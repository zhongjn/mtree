/*-------------------------------------------------------------------------
 *
 * mtreescan.h
 *	  routines defined in access/mtree/mtreescan.c
 *
 *
 * Portions Copyright (c) 1996-2020, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/access/mtreescan.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef MTREESCAN_H
#define MTREESCAN_H

#include "access/amapi.h"

extern IndexScanDesc mtreebeginscan(Relation r, int nkeys, int norderbys);
extern void mtreerescan(IndexScanDesc scan, ScanKey key, int nkeys,
					   ScanKey orderbys, int norderbys);
extern void mtreeendscan(IndexScanDesc scan);

#endif							/* MTREESCAN_H */
