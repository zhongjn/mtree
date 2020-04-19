#include "postgres.h"
#include "fmgr.h"
#include "access/amapi.h"
#include "commands/vacuum.h"
#include "mtree.h"
#include "mtree_private.h"
#include "mtreescan.h"
#include "mtreexlog.h"
#include "utils/index_selfuncs.h"

PG_MODULE_MAGIC;

void _PG_init(void);

void _PG_init(void) 
{
    ereport(INFO, 
        (errcode(ERRCODE_SUCCESSFUL_COMPLETION),
         errmsg("hello postgres!")));
}

PG_FUNCTION_INFO_V1(mtreehandler);

// Datum
// mtreehandler(PG_FUNCTION_ARGS)
// {
// 	IndexAmRoutine *amroutine = makeNode(IndexAmRoutine);

// 	amroutine->amstrategies = 1;
// 	amroutine->amsupport = 0;
// 	amroutine->amcanorder = false;
// 	amroutine->amcanorderbyop = true;
// 	amroutine->amcanbackward = false;
// 	amroutine->amcanunique = false;
// 	amroutine->amcanmulticol = false;
// 	amroutine->amoptionalkey = false;
// 	amroutine->amsearcharray = false;
// 	amroutine->amsearchnulls = false;
// 	amroutine->amstorage = false;
// 	amroutine->amclusterable = false;
// 	amroutine->ampredlocks = false;
// 	amroutine->amcanparallel = false;
// 	amroutine->amcaninclude = false;
// 	amroutine->amusemaintenanceworkmem = false;
// 	amroutine->amparallelvacuumoptions =
// 		VACUUM_OPTION_PARALLEL_BULKDEL | VACUUM_OPTION_PARALLEL_COND_CLEANUP;
// 	amroutine->amkeytype = InvalidOid;

// 	amroutine->ambuild = mtreebuild;
// 	amroutine->ambuildempty = mtreebuildempty;
// 	amroutine->aminsert = mtreeinsert;
// 	amroutine->ambulkdelete = mtreebulkdelete;
// 	amroutine->amvacuumcleanup = mtreevacuumcleanup;
// 	amroutine->amcanreturn = mtreecanreturn;
// 	amroutine->amcostestimate = gistcostestimate;
// 	amroutine->amoptions = mtreeoptions;
// 	amroutine->amproperty = mtreeproperty;
// 	amroutine->ambuildphasename = NULL;
// 	amroutine->amvalidate = mtreevalidate;
// 	amroutine->ambeginscan = mtreebeginscan;
// 	amroutine->amrescan = mtreerescan;
// 	amroutine->amgettuple = mtreegettuple;
// 	amroutine->amgetbitmap = mtreegetbitmap;
// 	amroutine->amendscan = mtreeendscan;
// 	amroutine->ammarkpos = NULL;
// 	amroutine->amrestrpos = NULL;
// 	amroutine->amestimateparallelscan = NULL;
// 	amroutine->aminitparallelscan = NULL;
// 	amroutine->amparallelrescan = NULL;

// 	PG_RETURN_POINTER(amroutine);
// }