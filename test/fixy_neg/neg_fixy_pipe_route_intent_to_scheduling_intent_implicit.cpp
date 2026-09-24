// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-077 HS14 fixture #1: `fixy::pipe::RouteIntent` and
// `fixy::pipe::SchedulingIntent` are surfaced as DISTINCT scoped enum
// classes — assigning a `RouteIntent` value where a `SchedulingIntent`
// is expected must fail, identically to the substrate's discipline.
//
// Why this matters: V-077 re-exports BOTH `RouteIntent` (the
// `AutoRouter` semantic-intent enum: Stream / Latest / Shardable /
// VariableCost) AND `SchedulingIntent` (the `AutoSplit` parallelism-
// appetite enum: LatencyCritical / Throughput / Background /
// Overlapped / Sequential / Adaptive).  A regression that conflates
// them (e.g., one underlying typedef alias `using RouteIntent =
// SchedulingIntent` or a macro mistake) would let the AutoRouter's
// intent silently populate the AutoSplit appetite slot — the planner
// would interpret `RouteIntent::Stream` (semantic-intent ordinal 0)
// as `SchedulingIntent::LatencyCritical` (appetite ordinal 0) and
// silently burn cores chasing wall-time on a routing-intent
// declaration that meant nothing of the sort.
//
// Distinct mismatch class (HS14 "≥2 distinct mismatch classes"):
// CROSS-ENUM-CONVERSION half — two distinct `enum class` types must
// not interconvert through the alias surface.  Sibling
// `neg_fixy_pipe_partition_to_schedule_mode_implicit.cpp` exercises
// the SAME-ENUMERATOR-NAME collision half via the AutoSplit axis
// enums that BOTH expose an `Inline` enumerator.
//
// Expected diagnostic: "cannot convert .* RouteIntent .* to .*
// SchedulingIntent" / "invalid conversion from
// 'crucible::concurrent::RouteIntent'" / "no known conversion".

#include <crucible/fixy/Pipe.h>

namespace fpipe = crucible::fixy::pipe;

int main() {
    // RouteIntent and SchedulingIntent are distinct scoped enum
    // classes.  Their values must NOT be interchangeable through the
    // alias surface — this assignment must fail to compile.
    fpipe::SchedulingIntent confused = fpipe::RouteIntent::Stream;
    (void)confused;
    return 0;
}
