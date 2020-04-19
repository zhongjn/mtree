/*-------------------------------------------------------------------------
 *
 * mtreeproc.c
 *	  Support procedures for MTrees over 2-D objects (boxes, polygons, circles,
 *	  points).
 *
 * This gives R-tree behavior, with Guttman's poly-time split algorithm.
 *
 *
 * Portions Copyright (c) 1996-2020, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * IDENTIFICATION
 *	src/backend/access/mtree/mtreeproc.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include <math.h>

#include "mtree.h"
#include "access/stratnum.h"
#include "utils/builtins.h"
#include "utils/float.h"
#include "utils/geo_decls.h"