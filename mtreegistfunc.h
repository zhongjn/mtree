#include "postgres.h"
#include "mtree_private.h"

bool mtree_consistent(MTREESTATE* state, MTREEENTRY *entry, Datum arg, StrategyNumber strategy,
                      Oid strategy_subtype, bool *recheck);

Datum mtree_union(MTREESTATE* state, MTreeEntryVector *evec, int *nbytes);

void mtree_penalty(MTREESTATE* state, MTREEENTRY *orig, MTREEENTRY *add, float *penalty);

void mtree_picksplit(MTREESTATE* state, MTreeEntryVector *evec, MTREE_SPLITVEC *sv);
