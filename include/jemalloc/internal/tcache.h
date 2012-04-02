/******************************************************************************/
#ifdef JEMALLOC_H_TYPES

typedef struct tcache_bin_info_s tcache_bin_info_t;
typedef struct tcache_bin_s tcache_bin_t;
typedef struct tcache_s tcache_t;

/*
 * tcache pointers close to NULL are used to encode state information that is
 * used for two purposes: preventing thread caching on a per thread basis and
 * cleaning up during thread shutdown.
 */
#define	TCACHE_STATE_DISABLED		((tcache_t *)(uintptr_t)1)
#define	TCACHE_STATE_REINCARNATED	((tcache_t *)(uintptr_t)2)
#define	TCACHE_STATE_PURGATORY		((tcache_t *)(uintptr_t)3)
#define	TCACHE_STATE_MAX		TCACHE_STATE_PURGATORY

/*
 * Absolute maximum number of cache slots for each small bin in the thread
 * cache.  This is an additional constraint beyond that imposed as: twice the
 * number of regions per run for this size class.
 *
 * This constant must be an even number.
 */
#define	TCACHE_NSLOTS_SMALL_MAX		200

/* Number of cache slots for large size classes. */
#define	TCACHE_NSLOTS_LARGE		20

/* (1U << opt_lg_tcache_max) is used to compute tcache_maxclass. */
#define	LG_TCACHE_MAXCLASS_DEFAULT	15

/*
 * TCACHE_GC_SWEEP is the approximate number of allocation events between
 * full GC sweeps.  Integer rounding may cause the actual number to be
 * slightly higher, since GC is performed incrementally.
 */
#define	TCACHE_GC_SWEEP			8192

/* Number of tcache allocation/deallocation events between incremental GCs. */
#define	TCACHE_GC_INCR							\
    ((TCACHE_GC_SWEEP / NBINS) + ((TCACHE_GC_SWEEP / NBINS == 0) ? 0 : 1))

#endif /* JEMALLOC_H_TYPES */
/******************************************************************************/
#ifdef JEMALLOC_H_STRUCTS

typedef enum {
	tcache_enabled_false   = 0, /* Enable cast to/from bool. */
	tcache_enabled_true    = 1,
	tcache_enabled_default = 2
} tcache_enabled_t;

/*
 * Read-only information associated with each element of tcache_t's tbins array
 * is stored separately, mainly to reduce memory usage.
 */
struct tcache_bin_info_s {
	unsigned	ncached_max;	/* Upper limit on ncached. */
};

struct tcache_bin_s {
	tcache_bin_stats_t tstats;
	int		low_water;	/* Min # cached since last GC. */
	unsigned	lg_fill_div;	/* Fill (ncached_max >> lg_fill_div). */
	unsigned	ncached;	/* # of cached objects. */
	void		**avail;	/* Stack of available objects. */
};

struct tcache_s {
	ql_elm(tcache_t) link;		/* Used for aggregating stats. */
	uint64_t	prof_accumbytes;/* Cleared after arena_prof_accum() */
	arena_t		*arena;		/* This thread's arena. */
	unsigned	ev_cnt;		/* Event count since incremental GC. */
	unsigned	next_gc_bin;	/* Next bin to GC. */
	tcache_bin_t	tbins[1];	/* Dynamically sized. */
	/*
	 * The pointer stacks associated with tbins follow as a contiguous
	 * array.  During tcache initialization, the avail pointer in each
	 * element of tbins is initialized to point to the proper offset within
	 * this array.
	 */
};

#endif /* JEMALLOC_H_STRUCTS */
/******************************************************************************/
#ifdef JEMALLOC_H_EXTERNS

extern bool	opt_tcache;
extern ssize_t	opt_lg_tcache_max;

extern tcache_bin_info_t	*tcache_bin_info;

/*
 * Number of tcache bins.  There are NBINS small-object bins, plus 0 or more
 * large-object bins.
 */
extern size_t			nhbins;

/* Maximum cached size class. */
extern size_t			tcache_maxclass;

void	tcache_bin_flush_small(tcache_bin_t *tbin, size_t binind, unsigned rem,
    tcache_t *tcache);
void	tcache_bin_flush_large(tcache_bin_t *tbin, size_t binind, unsigned rem,
    tcache_t *tcache);
void	tcache_arena_associate(tcache_t *tcache, arena_t *arena);
void	tcache_arena_dissociate(tcache_t *tcache);
tcache_t *tcache_create(arena_t *arena);
void	*tcache_alloc_small_hard(tcache_t *tcache, tcache_bin_t *tbin,
    size_t binind);
void	tcache_destroy(tcache_t *tcache);
void	tcache_thread_cleanup(void *arg);
void	tcache_stats_merge(tcache_t *tcache, arena_t *arena);
bool	tcache_boot0(void);
bool	tcache_boot1(void);

#endif /* JEMALLOC_H_EXTERNS */
/******************************************************************************/
#ifdef JEMALLOC_H_INLINES

#ifndef JEMALLOC_ENABLE_INLINE
malloc_tsd_protos(JEMALLOC_ATTR(unused), tcache, tcache_t *)
malloc_tsd_protos(JEMALLOC_ATTR(unused), tcache_enabled, tcache_enabled_t)

void	tcache_event(tcache_t *tcache);
void	tcache_flush(void);
bool	tcache_enabled_get(void);
tcache_t *tcache_get(bool create);
void	tcache_enabled_set(bool enabled);
void	*tcache_alloc_easy(tcache_bin_t *tbin);
void	*tcache_alloc_small(tcache_t *tcache, size_t size, bool zero);
void	*tcache_alloc_large(tcache_t *tcache, size_t size, bool zero);
void	tcache_dalloc_small(tcache_t *tcache, void *ptr);
void	tcache_dalloc_large(tcache_t *tcache, void *ptr, size_t size);
#endif

#if (defined(JEMALLOC_ENABLE_INLINE) || defined(JEMALLOC_TCACHE_C_))
/* Map of thread-specific caches. */
malloc_tsd_externs(tcache, tcache_t *)
malloc_tsd_funcs(JEMALLOC_INLINE, tcache, tcache_t *, NULL,
    tcache_thread_cleanup)
/* Per thread flag that allows thread caches to be disabled. */
malloc_tsd_externs(tcache_enabled, tcache_enabled_t)
malloc_tsd_funcs(JEMALLOC_INLINE, tcache_enabled, tcache_enabled_t,
    tcache_enabled_default, malloc_tsd_no_cleanup)

JEMALLOC_INLINE void
tcache_flush(void)
{
	tcache_t *tcache;

	cassert(config_tcache);

	tcache = *tcache_tsd_get();
	if ((uintptr_t)tcache <= (uintptr_t)TCACHE_STATE_MAX)
		return;
	tcache_destroy(tcache);
	tcache = NULL;
	tcache_tsd_set(&tcache);
}

JEMALLOC_INLINE bool
tcache_enabled_get(void)
{
	tcache_enabled_t tcache_enabled;

	cassert(config_tcache);

	tcache_enabled = *tcache_enabled_tsd_get();
	if (tcache_enabled == tcache_enabled_default) {
		tcache_enabled = (tcache_enabled_t)opt_tcache;
		tcache_enabled_tsd_set(&tcache_enabled);
	}

	return ((bool)tcache_enabled);
}

JEMALLOC_INLINE void
tcache_enabled_set(bool enabled)
{
	tcache_enabled_t tcache_enabled;
	tcache_t *tcache;

	cassert(config_tcache);

	tcache_enabled = (tcache_enabled_t)enabled;
	tcache_enabled_tsd_set(&tcache_enabled);
	tcache = *tcache_tsd_get();
	if (enabled) {
		if (tcache == TCACHE_STATE_DISABLED) {
			tcache = NULL;
			tcache_tsd_set(&tcache);
		}
	} else /* disabled */ {
		if (tcache > TCACHE_STATE_MAX) {
			tcache_destroy(tcache);
			tcache = NULL;
		}
		if (tcache == NULL) {
			tcache = TCACHE_STATE_DISABLED;
			tcache_tsd_set(&tcache);
		}
	}
}

JEMALLOC_INLINE tcache_t *
tcache_get(bool create)
{
	tcache_t *tcache;

	if (config_tcache == false)
		return (NULL);
	if (config_lazy_lock && isthreaded == false)
		return (NULL);

	tcache = *tcache_tsd_get();
	if ((uintptr_t)tcache <= (uintptr_t)TCACHE_STATE_MAX) {
		if (tcache == TCACHE_STATE_DISABLED)
			return (NULL);
		if (tcache == NULL) {
			if (create == false) {
				/*
				 * Creating a tcache here would cause
				 * allocation as a side effect of free().
				 * Ordinarily that would be okay since
				 * tcache_create() failure is a soft failure
				 * that doesn't propagate.  However, if TLS
				 * data are freed via free() as in glibc,
				 * subtle corruption could result from setting
				 * a TLS variable after its backing memory is
				 * freed.
				 */
				return (NULL);
			}
			if (tcache_enabled_get() == false) {
				tcache_enabled_set(false); /* Memoize. */
				return (NULL);
			}
			return (tcache_create(choose_arena()));
		}
		if (tcache == TCACHE_STATE_PURGATORY) {
			/*
			 * Make a note that an allocator function was called
			 * after tcache_thread_cleanup() was called.
			 */
			tcache = TCACHE_STATE_REINCARNATED;
			tcache_tsd_set(&tcache);
			return (NULL);
		}
		if (tcache == TCACHE_STATE_REINCARNATED)
			return (NULL);
		not_reached();
	}

	return (tcache);
}

JEMALLOC_INLINE void
tcache_event(tcache_t *tcache)
{

	if (TCACHE_GC_INCR == 0)
		return;

	tcache->ev_cnt++;
	assert(tcache->ev_cnt <= TCACHE_GC_INCR);
	if (tcache->ev_cnt == TCACHE_GC_INCR) {
		size_t binind = tcache->next_gc_bin;
		tcache_bin_t *tbin = &tcache->tbins[binind];
		tcache_bin_info_t *tbin_info = &tcache_bin_info[binind];

		if (tbin->low_water > 0) {
			/*
			 * Flush (ceiling) 3/4 of the objects below the low
			 * water mark.
			 */
			if (binind < NBINS) {
				tcache_bin_flush_small(tbin, binind,
				    tbin->ncached - tbin->low_water +
				    (tbin->low_water >> 2), tcache);
			} else {
				tcache_bin_flush_large(tbin, binind,
				    tbin->ncached - tbin->low_water +
				    (tbin->low_water >> 2), tcache);
			}
			/*
			 * Reduce fill count by 2X.  Limit lg_fill_div such that
			 * the fill count is always at least 1.
			 */
			if ((tbin_info->ncached_max >> (tbin->lg_fill_div+1))
			    >= 1)
				tbin->lg_fill_div++;
		} else if (tbin->low_water < 0) {
			/*
			 * Increase fill count by 2X.  Make sure lg_fill_div
			 * stays greater than 0.
			 */
			if (tbin->lg_fill_div > 1)
				tbin->lg_fill_div--;
		}
		tbin->low_water = tbin->ncached;

		tcache->next_gc_bin++;
		if (tcache->next_gc_bin == nhbins)
			tcache->next_gc_bin = 0;
		tcache->ev_cnt = 0;
	}
}

JEMALLOC_INLINE void *
tcache_alloc_easy(tcache_bin_t *tbin)
{
	void *ret;

	if (tbin->ncached == 0) {
		tbin->low_water = -1;
		return (NULL);
	}
	tbin->ncached--;
	if ((int)tbin->ncached < tbin->low_water)
		tbin->low_water = tbin->ncached;
	ret = tbin->avail[tbin->ncached];
	return (ret);
}

JEMALLOC_INLINE void *
tcache_alloc_small(tcache_t *tcache, size_t size, bool zero)
{
	void *ret;
	size_t binind;
	tcache_bin_t *tbin;

	binind = SMALL_SIZE2BIN(size);
	assert(binind < NBINS);
	tbin = &tcache->tbins[binind];
	ret = tcache_alloc_easy(tbin);
	if (ret == NULL) {
		ret = tcache_alloc_small_hard(tcache, tbin, binind);
		if (ret == NULL)
			return (NULL);
	}
	assert(arena_salloc(ret) == arena_bin_info[binind].reg_size);

	if (zero == false) {
		if (config_fill) {
			if (opt_junk)
				memset(ret, 0xa5, size);
			else if (opt_zero)
				memset(ret, 0, size);
		}
	} else
		memset(ret, 0, size);

	if (config_stats)
		tbin->tstats.nrequests++;
	if (config_prof)
		tcache->prof_accumbytes += arena_bin_info[binind].reg_size;
	tcache_event(tcache);
	return (ret);
}

JEMALLOC_INLINE void *
tcache_alloc_large(tcache_t *tcache, size_t size, bool zero)
{
	void *ret;
	size_t binind;
	tcache_bin_t *tbin;

	size = PAGE_CEILING(size);
	assert(size <= tcache_maxclass);
	binind = NBINS + (size >> LG_PAGE) - 1;
	assert(binind < nhbins);
	tbin = &tcache->tbins[binind];
	ret = tcache_alloc_easy(tbin);
	if (ret == NULL) {
		/*
		 * Only allocate one large object at a time, because it's quite
		 * expensive to create one and not use it.
		 */
		ret = arena_malloc_large(tcache->arena, size, zero);
		if (ret == NULL)
			return (NULL);
	} else {
		if (config_prof) {
			arena_chunk_t *chunk =
			    (arena_chunk_t *)CHUNK_ADDR2BASE(ret);
			size_t pageind = (((uintptr_t)ret - (uintptr_t)chunk) >>
			    LG_PAGE);
			chunk->map[pageind-map_bias].bits &=
			    ~CHUNK_MAP_CLASS_MASK;
		}
		if (zero == false) {
			if (config_fill) {
				if (opt_junk)
					memset(ret, 0xa5, size);
				else if (opt_zero)
					memset(ret, 0, size);
			}
		} else
			memset(ret, 0, size);

		if (config_stats)
			tbin->tstats.nrequests++;
		if (config_prof)
			tcache->prof_accumbytes += size;
	}

	tcache_event(tcache);
	return (ret);
}

JEMALLOC_INLINE void
tcache_dalloc_small(tcache_t *tcache, void *ptr)
{
	arena_t *arena;
	arena_chunk_t *chunk;
	arena_run_t *run;
	arena_bin_t *bin;
	tcache_bin_t *tbin;
	tcache_bin_info_t *tbin_info;
	size_t pageind, binind;
	arena_chunk_map_t *mapelm;

	assert(arena_salloc(ptr) <= SMALL_MAXCLASS);

	chunk = (arena_chunk_t *)CHUNK_ADDR2BASE(ptr);
	arena = chunk->arena;
	pageind = ((uintptr_t)ptr - (uintptr_t)chunk) >> LG_PAGE;
	mapelm = &chunk->map[pageind-map_bias];
	run = (arena_run_t *)((uintptr_t)chunk + (uintptr_t)((pageind -
	    (mapelm->bits >> LG_PAGE)) << LG_PAGE));
	bin = run->bin;
	binind = ((uintptr_t)bin - (uintptr_t)&arena->bins) /
	    sizeof(arena_bin_t);
	assert(binind < NBINS);

	if (config_fill && opt_junk)
		memset(ptr, 0x5a, arena_bin_info[binind].reg_size);

	tbin = &tcache->tbins[binind];
	tbin_info = &tcache_bin_info[binind];
	if (tbin->ncached == tbin_info->ncached_max) {
		tcache_bin_flush_small(tbin, binind, (tbin_info->ncached_max >>
		    1), tcache);
	}
	assert(tbin->ncached < tbin_info->ncached_max);
	tbin->avail[tbin->ncached] = ptr;
	tbin->ncached++;

	tcache_event(tcache);
}

JEMALLOC_INLINE void
tcache_dalloc_large(tcache_t *tcache, void *ptr, size_t size)
{
	size_t binind;
	tcache_bin_t *tbin;
	tcache_bin_info_t *tbin_info;

	assert((size & PAGE_MASK) == 0);
	assert(arena_salloc(ptr) > SMALL_MAXCLASS);
	assert(arena_salloc(ptr) <= tcache_maxclass);

	binind = NBINS + (size >> LG_PAGE) - 1;

	if (config_fill && opt_junk)
		memset(ptr, 0x5a, size);

	tbin = &tcache->tbins[binind];
	tbin_info = &tcache_bin_info[binind];
	if (tbin->ncached == tbin_info->ncached_max) {
		tcache_bin_flush_large(tbin, binind, (tbin_info->ncached_max >>
		    1), tcache);
	}
	assert(tbin->ncached < tbin_info->ncached_max);
	tbin->avail[tbin->ncached] = ptr;
	tbin->ncached++;

	tcache_event(tcache);
}
#endif

#endif /* JEMALLOC_H_INLINES */
/******************************************************************************/
