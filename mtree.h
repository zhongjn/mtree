/*-------------------------------------------------------------------------
 *
 * mtree.h
 *	  The public API for MTree indexes. This API is exposed to
 *	  individuals implementing MTree indexes, so backward-incompatible
 *	  changes should be made with care.
 *
 *
 * Portions Copyright (c) 1996-2020, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/access/mtree.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef MTREE_H
#define MTREE_H

#include "access/transam.h"
#include "access/xlog.h"
#include "access/xlogdefs.h"
#include "storage/block.h"
#include "storage/bufpage.h"
#include "utils/relcache.h"

/*
 * amproc indexes for MTree indexes.
 */
#define MTREE_DISTANCE_PROC				1
#define MTREENProcs						1

/*
 * Page opaque data in a MTree index page.
 */
#define F_LEAF				(1 << 0)	/* leaf page */
#define F_DELETED			(1 << 1)	/* the page has been deleted */
#define F_TUPLES_DELETED	(1 << 2)	/* some tuples on the page were
										 * deleted */
#define F_FOLLOW_RIGHT		(1 << 3)	/* page to the right has no downlink */
#define F_HAS_GARBAGE		(1 << 4)	/* some tuples on the page are dead,
										 * but not deleted yet */

typedef XLogRecPtr MTreeNSN;

/*
 * A bogus LSN / NSN value used during index build. Must be smaller than any
 * real or fake unlogged LSN, so that after an index build finishes, all the
 * splits are considered completed.
 */
#define MTreeBuildLSN	((XLogRecPtr) 1)

/*
 * For on-disk compatibility with pre-9.3 servers, NSN is stored as two
 * 32-bit fields on disk, same as LSNs.
 */
typedef PageXLogRecPtr PageMTreeNSN;

typedef struct MTREEPageOpaqueData
{
	PageMTreeNSN nsn;			/* this value must change on page split */
	BlockNumber rightlink;		/* next page if any */
	uint16		flags;			/* see bit definitions above */
	uint16		mtree_page_id;	/* for identification of MTree indexes */
} MTREEPageOpaqueData;

typedef MTREEPageOpaqueData *MTREEPageOpaque;

/*
 * The page ID is for the convenience of pg_filedump and similar utilities,
 * which otherwise would have a hard time telling pages of different index
 * types apart.  It should be the last 2 bytes on the page.  This is more or
 * less "free" due to alignment considerations.
 */
#define MTREE_PAGE_ID		0xFF81

/*
 * This is the Split Vector to be returned by the PickSplit method.
 * PickSplit should fill the indexes of tuples to go to the left side into
 * spl_left[], and those to go to the right into spl_right[] (note the method
 * is responsible for palloc'ing both of these arrays!).  The tuple counts
 * go into spl_nleft/spl_nright, and spl_ldatum/spl_rdatum must be set to
 * the union keys for each side.
 *
 * If spl_ldatum_exists and spl_rdatum_exists are true, then we are performing
 * a "secondary split" using a non-first index column.  In this case some
 * decisions have already been made about a page split, and the set of tuples
 * being passed to PickSplit is just the tuples about which we are undecided.
 * spl_ldatum/spl_rdatum then contain the union keys for the tuples already
 * chosen to go left or right.  Ideally the PickSplit method should take those
 * keys into account while deciding what to do with the remaining tuples, ie
 * it should try to "build out" from those unions so as to minimally expand
 * them.  If it does so, it should union the given tuples' keys into the
 * existing spl_ldatum/spl_rdatum values rather than just setting those values
 * from scratch, and then set spl_ldatum_exists/spl_rdatum_exists to false to
 * show it has done this.
 *
 * If the PickSplit method fails to clear spl_ldatum_exists/spl_rdatum_exists,
 * the core MTree code will make its own decision about how to merge the
 * secondary-split results with the previously-chosen tuples, and will then
 * recompute the union keys from scratch.  This is a workable though often not
 * optimal approach.
 */
typedef struct MTREE_SPLITVEC
{
	OffsetNumber *spl_left;		/* array of entries that go left */
	int			spl_nleft;		/* size of this array */
	Datum		spl_ldatum;		/* Union of keys in spl_left */
	bool		spl_ldatum_exists;	/* true, if spl_ldatum already exists. */

	OffsetNumber *spl_right;	/* array of entries that go right */
	int			spl_nright;		/* size of the array */
	Datum		spl_rdatum;		/* Union of keys in spl_right */
	bool		spl_rdatum_exists;	/* true, if spl_rdatum already exists. */
} MTREE_SPLITVEC;

/*
 * An entry on a MTree node.  Contains the key, as well as its own
 * location (rel,page,offset) which can supply the matching pointer.
 * leafkey is a flag to tell us if the entry is in a leaf node.
 */
typedef struct MTREEENTRY
{
	Datum		key;
	Relation	rel;
	Page		page;
	OffsetNumber offset;
	bool		leafkey;
} MTREEENTRY;

#define MTreePageGetOpaque(page) ( (MTREEPageOpaque) PageGetSpecialPointer(page) )

#define MTreePageIsLeaf(page)	( MTreePageGetOpaque(page)->flags & F_LEAF)
#define MTREE_LEAF(entry) (MTreePageIsLeaf((entry)->page))

#define MTreePageIsDeleted(page) ( MTreePageGetOpaque(page)->flags & F_DELETED)

#define MTreeTuplesDeleted(page) ( MTreePageGetOpaque(page)->flags & F_TUPLES_DELETED)
#define MTreeMarkTuplesDeleted(page) ( MTreePageGetOpaque(page)->flags |= F_TUPLES_DELETED)
#define MTreeClearTuplesDeleted(page)	( MTreePageGetOpaque(page)->flags &= ~F_TUPLES_DELETED)

#define MTreePageHasGarbage(page) ( MTreePageGetOpaque(page)->flags & F_HAS_GARBAGE)
#define MTreeMarkPageHasGarbage(page) ( MTreePageGetOpaque(page)->flags |= F_HAS_GARBAGE)
#define MTreeClearPageHasGarbage(page)	( MTreePageGetOpaque(page)->flags &= ~F_HAS_GARBAGE)

#define MTreeFollowRight(page) ( MTreePageGetOpaque(page)->flags & F_FOLLOW_RIGHT)
#define MTreeMarkFollowRight(page) ( MTreePageGetOpaque(page)->flags |= F_FOLLOW_RIGHT)
#define MTreeClearFollowRight(page)	( MTreePageGetOpaque(page)->flags &= ~F_FOLLOW_RIGHT)

#define MTreePageGetNSN(page) ( PageXLogRecPtrGet(MTreePageGetOpaque(page)->nsn))
#define MTreePageSetNSN(page, val) ( PageXLogRecPtrSet(MTreePageGetOpaque(page)->nsn, val))


/*
 * On a deleted page, we store this struct. A deleted page doesn't contain any
 * tuples, so we don't use the normal page layout with line pointers. Instead,
 * this struct is stored right after the standard page header. pd_lower points
 * to the end of this struct. If we add fields to this struct in the future, we
 * can distinguish the old and new formats by pd_lower.
 */
typedef struct MTREEDeletedPageContents
{
	/* last xid which could see the page in a scan */
	FullTransactionId deleteXid;
} MTREEDeletedPageContents;

static inline void
MTreePageSetDeleted(Page page, FullTransactionId deletexid)
{
	Assert(PageIsEmpty(page));

	MTreePageGetOpaque(page)->flags |= F_DELETED;
	((PageHeader) page)->pd_lower = MAXALIGN(SizeOfPageHeaderData) + sizeof(MTREEDeletedPageContents);

	((MTREEDeletedPageContents *) PageGetContents(page))->deleteXid = deletexid;
}

static inline FullTransactionId
MTreePageGetDeleteXid(Page page)
{
	Assert(MTreePageIsDeleted(page));

	/* Is the deleteXid field present? */
	if (((PageHeader) page)->pd_lower >= MAXALIGN(SizeOfPageHeaderData) +
		offsetof(MTREEDeletedPageContents, deleteXid) + sizeof(FullTransactionId))
	{
		return ((MTREEDeletedPageContents *) PageGetContents(page))->deleteXid;
	}
	else
		return FullTransactionIdFromEpochAndXid(0, FirstNormalTransactionId);
}

/*
 * Vector of MTREEENTRY structs; user-defined methods union and picksplit
 * take it as one of their arguments
 */
typedef struct
{
	int32		n;				/* number of elements */
	MTREEENTRY	vector[FLEXIBLE_ARRAY_MEMBER];
} MTreeEntryVector;

#define GEVHDRSZ	(offsetof(MTreeEntryVector, vector))

/*
 * macro to initialize a MTREEENTRY
 */
#define mtreeentryinit(e, k, r, pg, o, l) \
	do { (e).key = (k); (e).rel = (r); (e).page = (pg); \
		 (e).offset = (o); (e).leafkey = (l); } while (0)

#endif							/* MTREE_H */
