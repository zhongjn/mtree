/*-------------------------------------------------------------------------
 *
 * mtreexlog.h
 *	  mtree xlog routines
 *
 * Portions Copyright (c) 1996-2020, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/access/mtreexlog.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef MTREE_XLOG_H
#define MTREE_XLOG_H

#include "mtree.h"
#include "access/xlogreader.h"
#include "lib/stringinfo.h"

#define XLOG_MTREE_PAGE_UPDATE		0x00
#define XLOG_MTREE_DELETE			0x10	/* delete leaf index tuples for a
											 * page */
#define XLOG_MTREE_PAGE_REUSE		0x20	/* old page is about to be reused
											 * from FSM */
#define XLOG_MTREE_PAGE_SPLIT		0x30
 /* #define XLOG_MTREE_INSERT_COMPLETE	 0x40 */	/* not used anymore */
 /* #define XLOG_MTREE_CREATE_INDEX		 0x50 */	/* not used anymore */
#define XLOG_MTREE_PAGE_DELETE		0x60

/*
 * Backup Blk 0: updated page.
 * Backup Blk 1: If this operation completes a page split, by inserting a
 *				 downlink for the split page, the left half of the split
 */
typedef struct mtreexlogPageUpdate
{
	/* number of deleted offsets */
	uint16		ntodelete;
	uint16		ntoinsert;

	/*
	 * In payload of blk 0 : 1. todelete OffsetNumbers 2. tuples to insert
	 */
} mtreexlogPageUpdate;

/*
 * Backup Blk 0: Leaf page, whose index tuples are deleted.
 */
typedef struct mtreexlogDelete
{
	TransactionId latestRemovedXid;
	uint16		ntodelete;		/* number of deleted offsets */

	/*
	 * In payload of blk 0 : todelete OffsetNumbers
	 */
} mtreexlogDelete;

#define SizeOfMTreexlogDelete	(offsetof(mtreexlogDelete, ntodelete) + sizeof(uint16))

/*
 * Backup Blk 0: If this operation completes a page split, by inserting a
 *				 downlink for the split page, the left half of the split
 * Backup Blk 1 - npage: split pages (1 is the original page)
 */
typedef struct mtreexlogPageSplit
{
	BlockNumber origrlink;		/* rightlink of the page before split */
	MTreeNSN		orignsn;		/* NSN of the page before split */
	bool		origleaf;		/* was splitted page a leaf page? */

	uint16		npage;			/* # of pages in the split */
	bool		markfollowright;	/* set F_FOLLOW_RIGHT flags */

	/*
	 * follow: 1. mtreexlogPage and array of IndexTupleData per page
	 */
} mtreexlogPageSplit;

/*
 * Backup Blk 0: page that was deleted.
 * Backup Blk 1: parent page, containing the downlink to the deleted page.
 */
typedef struct mtreexlogPageDelete
{
	FullTransactionId deleteXid;	/* last Xid which could see page in scan */
	OffsetNumber downlinkOffset;	/* Offset of downlink referencing this
									 * page */
} mtreexlogPageDelete;

#define SizeOfMTreexlogPageDelete	(offsetof(mtreexlogPageDelete, downlinkOffset) + sizeof(OffsetNumber))


/*
 * This is what we need to know about page reuse, for hot standby.
 */
typedef struct mtreexlogPageReuse
{
	RelFileNode node;
	BlockNumber block;
	FullTransactionId latestRemovedFullXid;
} mtreexlogPageReuse;

#define SizeOfMTreexlogPageReuse	(offsetof(mtreexlogPageReuse, latestRemovedFullXid) + sizeof(FullTransactionId))

extern void mtree_redo(XLogReaderState *record);
extern void mtree_desc(StringInfo buf, XLogReaderState *record);
extern const char *mtree_identify(uint8 info);
extern void mtree_xlog_startup(void);
extern void mtree_xlog_cleanup(void);
extern void mtree_mask(char *pagedata, BlockNumber blkno);

#endif
