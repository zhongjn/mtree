/*-------------------------------------------------------------------------
 *
 * mtree_private.h
 *	  private declarations for MTree -- declarations related to the
 *	  internal implementation of MTree, not the public API
 *
 * Portions Copyright (c) 1996-2020, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/access/mtree_private.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef MTREE_PRIVATE_H
#define MTREE_PRIVATE_H

#include "access/amapi.h"
#include "mtree.h"
#include "access/itup.h"
#include "lib/pairingheap.h"
#include "storage/bufmgr.h"
#include "storage/buffile.h"
#include "utils/hsearch.h"
#include "access/genam.h"

/*
 * Maximum number of "halves" a page can be split into in one operation.
 * Typically a split produces 2 halves, but can be more if keys have very
 * different lengths, or when inserting multiple keys in one operation (as
 * when inserting downlinks to an internal node).  There is no theoretical
 * limit on this, but in practice if you get more than a handful page halves
 * in one split, there's something wrong with the opclass implementation.
 * MTREE_MAX_SPLIT_PAGES is an arbitrary limit on that, used to size some
 * local arrays used during split.  Note that there is also a limit on the
 * number of buffers that can be held locked at a time, MAX_SIMUL_LWLOCKS,
 * so if you raise this higher than that limit, you'll just get a different
 * error.
 */
#define MTREE_MAX_SPLIT_PAGES		75

/* Buffer lock modes */
#define MTREE_SHARE	BUFFER_LOCK_SHARE
#define MTREE_EXCLUSIVE	BUFFER_LOCK_EXCLUSIVE
#define MTREE_UNLOCK BUFFER_LOCK_UNLOCK

typedef struct
{
	BlockNumber prev;
	uint32		freespace;
	char		tupledata[FLEXIBLE_ARRAY_MEMBER];
} MTREENodeBufferPage;

#define BUFFER_PAGE_DATA_OFFSET MAXALIGN(offsetof(MTREENodeBufferPage, tupledata))
/* Returns free space in node buffer page */
#define PAGE_FREE_SPACE(nbp) (nbp->freespace)
/* Checks if node buffer page is empty */
#define PAGE_IS_EMPTY(nbp) (nbp->freespace == BLCKSZ - BUFFER_PAGE_DATA_OFFSET)
/* Checks if node buffers page don't contain sufficient space for index tuple */
#define PAGE_NO_SPACE(nbp, itup) (PAGE_FREE_SPACE(nbp) < \
										MAXALIGN(IndexTupleSize(itup)))

/*
 * MTREESTATE: information needed for any MTree index operation
 *
 * This struct retains call info for the index's opclass-specific support
 * functions (per index column), plus the index's tuple descriptor.
 *
 * scanCxt holds the MTREESTATE itself as well as any data that lives for the
 * lifetime of the index operation.  We pass this to the support functions
 * via fn_mcxt, so that they can store scan-lifespan data in it.  The
 * functions are invoked in tempCxt, which is typically short-lifespan
 * (that is, it's reset after each tuple).  However, tempCxt can be the same
 * as scanCxt if we're not bothering with per-tuple context resets.
 */
typedef struct MTREESTATE
{
	MemoryContext scanCxt;		/* context for scan-lifespan data */
	MemoryContext tempCxt;		/* short-term context for calling functions */

	TupleDesc	leafTupdesc;	/* index's tuple descriptor */
	TupleDesc	nonLeafTupdesc; /* truncated tuple descriptor for non-leaf
								 * pages */
	TupleDesc	fetchTupdesc;	/* tuple descriptor for tuples returned in an
								 * index-only scan */

	// FmgrInfo	consistentFn[INDEX_MAX_KEYS];
	// FmgrInfo	unionFn[INDEX_MAX_KEYS];
	// FmgrInfo	compressFn[INDEX_MAX_KEYS];
	// FmgrInfo	decompressFn[INDEX_MAX_KEYS];
	// FmgrInfo	penaltyFn[INDEX_MAX_KEYS];
	// FmgrInfo	picksplitFn[INDEX_MAX_KEYS];
	FmgrInfo	equalFn; // useful
	FmgrInfo	distanceFn; // useful
	// FmgrInfo	fetchFn[INDEX_MAX_KEYS];

	/* Collations to pass to the support functions */
	Oid			supportCollation[INDEX_MAX_KEYS];
} MTREESTATE;


/*
 * During a MTree index search, we must maintain a queue of unvisited items,
 * which can be either individual heap tuples or whole index pages.  If it
 * is an ordered search, the unvisited items should be visited in distance
 * order.  Unvisited items at the same distance should be visited in
 * depth-first order, that is heap items first, then lower index pages, then
 * upper index pages; this rule avoids doing extra work during a search that
 * ends early due to LIMIT.
 *
 * To perform an ordered search, we use a pairing heap to manage the
 * distance-order queue.  In a non-ordered search (no order-by operators),
 * we use it to return heap tuples before unvisited index pages, to
 * ensure depth-first order, but all entries are otherwise considered
 * equal.
 */

/* Individual heap tuple to be visited */
typedef struct MTREESearchHeapItem
{
	ItemPointerData heapPtr;
	bool		recheck;		/* T if quals must be rechecked */
	bool		recheckDistances;	/* T if distances must be rechecked */
	HeapTuple	recontup;		/* data reconstructed from the index, used in
								 * index-only scans */
	OffsetNumber offnum;		/* track offset in page to mark tuple as
								 * LP_DEAD */
} MTREESearchHeapItem;

/* Unvisited item, either index page or heap tuple */
typedef struct MTREESearchItem
{
	pairingheap_node phNode;
	BlockNumber blkno;			/* index page number, or InvalidBlockNumber */
	union
	{
		MTreeNSN		parentlsn;	/* parent page's LSN, if index page */
		/* we must store parentlsn to detect whether a split occurred */
		MTREESearchHeapItem heap;	/* heap info, if heap tuple */
	}			data;

	/* numberOfOrderBys entries */
	IndexOrderByDistance distances[FLEXIBLE_ARRAY_MEMBER];
} MTREESearchItem;

#define MTREESearchItemIsHeap(item)	((item).blkno == InvalidBlockNumber)

#define SizeOfMTREESearchItem(n_distances) \
	(offsetof(MTREESearchItem, distances) + \
	 sizeof(IndexOrderByDistance) * (n_distances))

/*
 * MTREEScanOpaqueData: private state for a scan of a MTree index
 */
typedef struct MTREEScanOpaqueData
{
	MTREESTATE  *mtreestate;		/* index information, see above */
	Oid		   *orderByTypes;	/* datatypes of ORDER BY expressions */

	pairingheap *queue;			/* queue of unvisited items */
	MemoryContext queueCxt;		/* context holding the queue */
	bool		qual_ok;		/* false if qual can never be satisfied */
	bool		firstCall;		/* true until first mtreegettuple call */

	/* pre-allocated workspace arrays */
	IndexOrderByDistance *distances;	/* output area for mtreeindex_keytest */

	/* info about killed items if any (killedItems is NULL if never used) */
	OffsetNumber *killedItems;	/* offset numbers of killed items */
	int			numKilled;		/* number of currently stored items */
	BlockNumber curBlkno;		/* current number of block */
	MTreeNSN		curPageLSN;		/* pos in the WAL stream when page was read */

	/* In a non-ordered search, returnable heap items are stored here: */
	MTREESearchHeapItem pageData[BLCKSZ / sizeof(IndexTupleData)];
	OffsetNumber nPageData;		/* number of valid items in array */
	OffsetNumber curPageData;	/* next item to return */
	MemoryContext pageDataCxt;	/* context holding the fetched tuples, for
								 * index-only scans */
} MTREEScanOpaqueData;

typedef MTREEScanOpaqueData *MTREEScanOpaque;

/* despite the name, mtreexlogPage is not part of any xlog record */
typedef struct mtreexlogPage
{
	BlockNumber blkno;
	int			num;			/* number of index tuples following */
} mtreexlogPage;

/* SplitedPageLayout - mtreeSplit function result */
typedef struct SplitedPageLayout
{
	mtreexlogPage block;
	IndexTupleData *list;
	int			lenlist;
	IndexTuple	itup;			/* union key for page */
	Page		page;			/* to operate */
	Buffer		buffer;			/* to write after all proceed */

	struct SplitedPageLayout *next;
} SplitedPageLayout;

/*
 * MTREEInsertStack used for locking buffers and transfer arguments during
 * insertion
 */
typedef struct MTREEInsertStack
{
	/* current page */
	BlockNumber blkno;
	Buffer		buffer;
	Page		page;

	/*
	 * log sequence number from page->lsn to recognize page update and compare
	 * it with page's nsn to recognize page split
	 */
	MTreeNSN		lsn;

	/*
	 * If set, we split the page while descending the tree to find an
	 * insertion target. It means that we need to retry from the parent,
	 * because the downlink of this page might no longer cover the new key.
	 */
	bool		retry_from_parent;

	/* offset of the downlink in the parent page, that points to this page */
	OffsetNumber downlinkoffnum;

	/* pointer to parent */
	struct MTREEInsertStack *parent;
} MTREEInsertStack;

/* Working state and results for multi-column split logic in mtreesplit.c */
typedef struct MTreeSplitVector
{
	MTREE_SPLITVEC splitVector;	/* passed to/from user PickSplit method */

	Datum		spl_lattr[INDEX_MAX_KEYS];	/* Union of subkeys in
											 * splitVector.spl_left */
	bool		spl_lisnull[INDEX_MAX_KEYS];

	Datum		spl_rattr[INDEX_MAX_KEYS];	/* Union of subkeys in
											 * splitVector.spl_right */
	bool		spl_risnull[INDEX_MAX_KEYS];

	bool	   *spl_dontcare;	/* flags tuples which could go to either side
								 * of the split for zero penalty */
} MTreeSplitVector;

typedef struct
{
	Relation	r;
	Relation	heapRel;
	Size		freespace;		/* free space to be left */
	bool		is_build;

	MTREEInsertStack *stack;
} MTREEInsertState;

/* root page of a mtree index */
#define MTREE_ROOT_BLKNO				0

/*
 * Before PostgreSQL 9.1, we used to rely on so-called "invalid tuples" on
 * inner pages to finish crash recovery of incomplete page splits. If a crash
 * happened in the middle of a page split, so that the downlink pointers were
 * not yet inserted, crash recovery inserted a special downlink pointer. The
 * semantics of an invalid tuple was that it if you encounter one in a scan,
 * it must always be followed, because we don't know if the tuples on the
 * child page match or not.
 *
 * We no longer create such invalid tuples, we now mark the left-half of such
 * an incomplete split with the F_FOLLOW_RIGHT flag instead, and finish the
 * split properly the next time we need to insert on that page. To retain
 * on-disk compatibility for the sake of pg_upgrade, we still store 0xffff as
 * the offset number of all inner tuples. If we encounter any invalid tuples
 * with 0xfffe during insertion, we throw an error, though scans still handle
 * them. You should only encounter invalid tuples if you pg_upgrade a pre-9.1
 * mtree index which already has invalid tuples in it because of a crash. That
 * should be rare, and you are recommended to REINDEX anyway if you have any
 * invalid tuples in an index, so throwing an error is as far as we go with
 * supporting that.
 */
#define TUPLE_IS_VALID		0xffff
#define TUPLE_IS_INVALID	0xfffe

#define  MTreeTupleIsInvalid(itup)	( ItemPointerGetOffsetNumber( &((itup)->t_tid) ) == TUPLE_IS_INVALID )
#define  MTreeTupleSetValid(itup)	ItemPointerSetOffsetNumber( &((itup)->t_tid), TUPLE_IS_VALID )




/*
 * A buffer attached to an internal node, used when building an index in
 * buffering mode.
 */
typedef struct
{
	BlockNumber nodeBlocknum;	/* index block # this buffer is for */
	int32		blocksCount;	/* current # of blocks occupied by buffer */

	BlockNumber pageBlocknum;	/* temporary file block # */
	MTREENodeBufferPage *pageBuffer; /* in-memory buffer page */

	/* is this buffer queued for emptying? */
	bool		queuedForEmptying;

	/* is this a temporary copy, not in the hash table? */
	bool		isTemp;

	int			level;			/* 0 == leaf */
} MTREENodeBuffer;

/*
 * Does specified level have buffers? (Beware of multiple evaluation of
 * arguments.)
 */
#define LEVEL_HAS_BUFFERS(nlevel, gfbb) \
	((nlevel) != 0 && (nlevel) % (gfbb)->levelStep == 0 && \
	 (nlevel) != (gfbb)->rootlevel)

/* Is specified buffer at least half-filled (should be queued for emptying)? */
#define BUFFER_HALF_FILLED(nodeBuffer, gfbb) \
	((nodeBuffer)->blocksCount > (gfbb)->pagesPerBuffer / 2)

/*
 * Is specified buffer full? Our buffers can actually grow indefinitely,
 * beyond the "maximum" size, so this just means whether the buffer has grown
 * beyond the nominal maximum size.
 */
#define BUFFER_OVERFLOWED(nodeBuffer, gfbb) \
	((nodeBuffer)->blocksCount > (gfbb)->pagesPerBuffer)

/*
 * Data structure with general information about build buffers.
 */
typedef struct MTREEBuildBuffers
{
	/* Persistent memory context for the buffers and metadata. */
	MemoryContext context;

	BufFile    *pfile;			/* Temporary file to store buffers in */
	long		nFileBlocks;	/* Current size of the temporary file */

	/*
	 * resizable array of free blocks.
	 */
	long	   *freeBlocks;
	int			nFreeBlocks;	/* # of currently free blocks in the array */
	int			freeBlocksLen;	/* current allocated length of the array */

	/* Hash for buffers by block number */
	HTAB	   *nodeBuffersTab;

	/* List of buffers scheduled for emptying */
	List	   *bufferEmptyingQueue;

	/*
	 * Parameters to the buffering build algorithm. levelStep determines which
	 * levels in the tree have buffers, and pagesPerBuffer determines how
	 * large each buffer is.
	 */
	int			levelStep;
	int			pagesPerBuffer;

	/* Array of lists of buffers on each level, for final emptying */
	List	  **buffersOnLevels;
	int			buffersOnLevelsLen;

	/*
	 * Dynamically-sized array of buffers that currently have their last page
	 * loaded in main memory.
	 */
	MTREENodeBuffer **loadedBuffers;
	int			loadedBuffersCount; /* # of entries in loadedBuffers */
	int			loadedBuffersLen;	/* allocated size of loadedBuffers */

	/* Level of the current root node (= height of the index tree - 1) */
	int			rootlevel;
} MTREEBuildBuffers;

/* MTreeOptions->buffering_mode values */
typedef enum MTreeOptBufferingMode
{
	MTREE_OPTION_BUFFERING_AUTO,
	MTREE_OPTION_BUFFERING_ON,
	MTREE_OPTION_BUFFERING_OFF
} MTreeOptBufferingMode;

/*
 * Storage type for MTree's reloptions
 */
typedef struct MTreeOptions
{
	int32		vl_len_;		/* varlena header (do not touch directly!) */
	int			fillfactor;		/* page fill factor in percent (0..100) */
	MTreeOptBufferingMode buffering_mode;	/* buffering build mode */
} MTreeOptions;

/* mtree.c */
extern void mtreebuildempty(Relation index);
extern bool mtreeinsert(Relation r, Datum *values, bool *isnull,
					   ItemPointer ht_ctid, Relation heapRel,
					   IndexUniqueCheck checkUnique,
					   struct IndexInfo *indexInfo);
extern MemoryContext createTempMTreeContext(void);
extern MTREESTATE *initMTREEstate(Relation index);
extern void freeMTREEstate(MTREESTATE *mtreestate);
extern void mtreedoinsert(Relation r,
						 IndexTuple itup,
						 Size freespace,
						 MTREESTATE *mtreestate,
						 Relation heapRel,
						 bool is_build);

/* A List of these is returned from mtreeplacetopage() in *splitinfo */
typedef struct
{
	Buffer		buf;			/* the split page "half" */
	IndexTuple	downlink;		/* downlink for this half. */
} MTREEPageSplitInfo;

extern bool mtreeplacetopage(Relation rel, Size freespace, MTREESTATE *mtreestate,
							Buffer buffer,
							IndexTuple *itup, int ntup,
							OffsetNumber oldoffnum, BlockNumber *newblkno,
							Buffer leftchildbuf,
							List **splitinfo,
							bool markfollowright,
							Relation heapRel,
							bool is_build);

extern SplitedPageLayout *mtreeSplit(Relation r, Page page, IndexTuple *itup,
									int len, MTREESTATE *mtreestate);

/* mtreexlog.c */
extern XLogRecPtr mtreeXLogPageDelete(Buffer buffer,
									 FullTransactionId xid, Buffer parentBuffer,
									 OffsetNumber downlinkOffset);

extern void mtreeXLogPageReuse(Relation rel, BlockNumber blkno,
							  FullTransactionId latestRemovedXid);

extern XLogRecPtr mtreeXLogUpdate(Buffer buffer,
								 OffsetNumber *todelete, int ntodelete,
								 IndexTuple *itup, int ntup,
								 Buffer leftchild);

extern XLogRecPtr mtreeXLogDelete(Buffer buffer, OffsetNumber *todelete,
								 int ntodelete, TransactionId latestRemovedXid);

extern XLogRecPtr mtreeXLogSplit(bool page_is_leaf,
								SplitedPageLayout *dist,
								BlockNumber origrlink, MTreeNSN oldnsn,
								Buffer leftchild, bool markfollowright);

/* mtreeget.c */
extern bool mtreegettuple(IndexScanDesc scan, ScanDirection dir);
extern int64 mtreegetbitmap(IndexScanDesc scan, TIDBitmap *tbm);
extern bool mtreecanreturn(Relation index, int attno);

/* mtreevalidate.c */
extern bool mtreevalidate(Oid opclassoid);

/* mtreeutil.c */

#define MTreePageSize   \
	( BLCKSZ - SizeOfPageHeaderData - MAXALIGN(sizeof(MTREEPageOpaqueData)) )

#define MTREE_MIN_FILLFACTOR			10
#define MTREE_DEFAULT_FILLFACTOR		90

extern bytea *mtreeoptions(Datum reloptions, bool validate);
extern bool mtreeproperty(Oid index_oid, int attno,
						 IndexAMProperty prop, const char *propname,
						 bool *res, bool *isnull);
extern bool mtreefitpage(IndexTuple *itvec, int len);
extern bool mtreenospace(Page page, IndexTuple *itvec, int len, OffsetNumber todelete, Size freespace);
extern void mtreecheckpage(Relation rel, Buffer buf);
extern Buffer mtreeNewBuffer(Relation r);
extern bool mtreePageRecyclable(Page page);
extern void mtreefillbuffer(Page page, IndexTuple *itup, int len,
						   OffsetNumber off);
extern IndexTuple *mtreeextractpage(Page page, int *len /* out */ );
extern IndexTuple *mtreejoinvector(IndexTuple *itvec, int *len,
								  IndexTuple *additvec, int addlen);
extern IndexTupleData *mtreefillitupvec(IndexTuple *vec, int veclen, int *memlen);

extern IndexTuple mtreeunion(Relation r, IndexTuple *itvec,
							int len, MTREESTATE *mtreestate);
extern IndexTuple mtreegetadjusted(Relation r,
								  IndexTuple oldtup,
								  IndexTuple addtup,
								  MTREESTATE *mtreestate);
extern IndexTuple mtreeFormTuple(MTREESTATE *mtreestate,
								Relation r, Datum *attdata, bool *isnull, bool isleaf);

extern OffsetNumber mtreechoose(Relation r, Page p,
							   IndexTuple it,
							   MTREESTATE *mtreestate);

extern void MTREEInitBuffer(Buffer b, uint32 f);
extern void mtreedentryinit(MTREESTATE *mtreestate, int nkey, MTREEENTRY *e,
						   Datum k, Relation r, Page pg, OffsetNumber o,
						   bool l, bool isNull);

extern float mtreepenalty(MTREESTATE *mtreestate, int attno,
						 MTREEENTRY *key1, bool isNull1,
						 MTREEENTRY *key2, bool isNull2);
extern void mtreeMakeUnionItVec(MTREESTATE *mtreestate, IndexTuple *itvec, int len,
							   Datum *attr, bool *isnull);
extern bool mtreeKeyIsEQ(MTREESTATE *mtreestate, int attno, Datum a, Datum b);
extern void mtreeDeCompressAtt(MTREESTATE *mtreestate, Relation r, IndexTuple tuple, Page p,
							  OffsetNumber o, MTREEENTRY *attdata, bool *isnull);
extern HeapTuple mtreeFetchTuple(MTREESTATE *mtreestate, Relation r,
								IndexTuple tuple);
extern void mtreeMakeUnionKey(MTREESTATE *mtreestate, int attno,
							 MTREEENTRY *entry1, bool isnull1,
							 MTREEENTRY *entry2, bool isnull2,
							 Datum *dst, bool *dstisnull);

extern XLogRecPtr mtreeGetFakeLSN(Relation rel);

/* mtreevacuum.c */
extern IndexBulkDeleteResult *mtreebulkdelete(IndexVacuumInfo *info,
											 IndexBulkDeleteResult *stats,
											 IndexBulkDeleteCallback callback,
											 void *callback_state);
extern IndexBulkDeleteResult *mtreevacuumcleanup(IndexVacuumInfo *info,
												IndexBulkDeleteResult *stats);

/* mtreesplit.c */
extern void mtreeSplitByKey(Relation r, Page page, IndexTuple *itup,
						   int len, MTREESTATE *mtreestate,
						   MTreeSplitVector *v,
						   int attno);

/* mtreebuild.c */
extern IndexBuildResult *mtreebuild(Relation heap, Relation index,
								   struct IndexInfo *indexInfo);
extern void mtreeValidateBufferingOption(const char *value);

/* mtreebuildbuffers.c */
extern MTREEBuildBuffers *mtreeInitBuildBuffers(int pagesPerBuffer, int levelStep,
											  int maxLevel);
extern MTREENodeBuffer *mtreeGetNodeBuffer(MTREEBuildBuffers *gfbb,
										 MTREESTATE *mtreestate,
										 BlockNumber blkno, int level);
extern void mtreePushItupToNodeBuffer(MTREEBuildBuffers *gfbb,
									 MTREENodeBuffer *nodeBuffer, IndexTuple item);
extern bool mtreePopItupFromNodeBuffer(MTREEBuildBuffers *gfbb,
									  MTREENodeBuffer *nodeBuffer, IndexTuple *item);
extern void mtreeFreeBuildBuffers(MTREEBuildBuffers *gfbb);
extern void mtreeRelocateBuildBuffersOnSplit(MTREEBuildBuffers *gfbb,
											MTREESTATE *mtreestate, Relation r,
											int level, Buffer buffer,
											List *splitinfo);
extern void mtreeUnloadNodeBuffers(MTREEBuildBuffers *gfbb);

#endif							/* MTREE_PRIVATE_H */
