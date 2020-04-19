#include "postgres.h"
#include "mtree_private.h"

bool mtree_consistent(MTREESTATE* state, MTREEENTRY *entry, Datum arg, StrategyNumber strategy,
                      Oid strategy_subtype, bool *recheck) 
{
    
    

    return false;
}


Datum mtree_union(MTreeEntryVector *evec, int *nbytes) { return 0; }

void mtree_penalty(MTREEENTRY *orig, MTREEENTRY *add, float *penalty) {}

void mtree_picksplit(MTreeEntryVector *evec, MTREE_SPLITVEC *sv) {}
