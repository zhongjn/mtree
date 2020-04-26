#include "mtreegistfunc.h"

struct RoutingObject
{
    float radius;
    
}

bool mtree_consistent(MTREESTATE* state, MTREEENTRY *entry, Datum arg, StrategyNumber strategy,
                      Oid strategy_subtype, bool *recheck)
{
    // TODO: implement consistent
    if (entry->leafkey)
    {
        // same?
    }
    else
    {
        // consider distance to parent
        Datum dist = FunctionCall5(state->distanceFn, );
        float4 fdist = DatumGetFloat4(dist);
        float4 radius = 0; // TODO
        if (fdist <= radius)
        {
            return true;
        }
    }
    return false;
}                      

Datum mtree_union(MTREESTATE* state, MTreeEntryVector *evec, int *nbytes)
{
    // TODO: implement union
    return 0;
}

void mtree_penalty(MTREESTATE* state, MTREEENTRY *orig, MTREEENTRY *add, float *penalty)
{
    // TODO: implement penalty
}

void mtree_picksplit(MTREESTATE* state, MTreeEntryVector *evec, MTREE_SPLITVEC *sv)
{
    // TODO: implement picksplit
}