// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-077 HS14 fixture #2: `fixy::pipe::AutoSplitPartitionStrategy`
// and `fixy::pipe::AutoSplitScheduleMode` are surfaced as DISTINCT
// scoped enum classes that BOTH expose an `Inline` enumerator —
// a typedef regression that conflated them would let cross-axis
// values slip through silently, because the source-side `::Inline`
// spelling is valid on either type.
//
// Why this matters: V-077 re-exports the AutoSplit decision quartet:
//
//   AutoSplitPartitionStrategy  — { Inline, EvenContiguous }
//   AutoSplitScheduleMode       — { Inline, SyncForkJoin }
//   AutoSplitPlacementPolicy    — { Caller, PoolAny, PoolNumaLocal,
//                                   PoolNumaSpread }
//   AutoSplitCompletionMode     — { None, BlockingWait }
//
// The Partition and ScheduleMode enums BOTH define an `Inline`
// enumerator (the partition strategy says "do not partition; inline
// the work"; the schedule mode says "do not schedule; inline the
// call").  Conflating these via a typedef alias would let:
//
//     decision.schedule = AutoSplitPartitionStrategy::Inline;
//
// compile silently — the planner would receive a wrong-axis value.
// This neg-fixture pins them as DISTINCT enum classes through the
// fixy::pipe:: alias surface.
//
// Distinct mismatch class (HS14 "≥2 distinct mismatch classes"):
// SAME-ENUMERATOR-NAME-COLLISION half — two enum classes that share
// an enumerator NAME (`Inline`) must still reject cross-assignment.
// Sibling `neg_fixy_pipe_route_intent_to_scheduling_intent_implicit
// .cpp` exercises the disjoint-name CROSS-ENUM-CONVERSION half.
//
// Expected diagnostic: "cannot convert .*
// AutoSplitPartitionStrategy .* to .* AutoSplitScheduleMode" /
// "invalid conversion from
// 'crucible::concurrent::AutoSplitPartitionStrategy'" /
// "no known conversion".

#include <crucible/fixy/Pipe.h>

namespace fpipe = crucible::fixy::pipe;

int main() {
    // Both Partition::Inline and Schedule::Inline name an `Inline`
    // enumerator.  The enum-class scoping plus the type-distinctness
    // of the using-decls must reject the cross-axis assignment.
    fpipe::AutoSplitScheduleMode confused =
        fpipe::AutoSplitPartitionStrategy::Inline;
    (void)confused;
    return 0;
}
