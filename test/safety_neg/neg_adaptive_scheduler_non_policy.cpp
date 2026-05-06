// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Pool is policy-parametric, not duck-typed over arbitrary tags.  The
// SchedulerPolicy concept must reject a type with no queue_template,
// policy_tag, priority_kind, needs_topology, or name() surface.

#include <crucible/concurrent/AdaptiveScheduler.h>

struct NotSchedulerPolicy {};

int main() {
    crucible::concurrent::Pool<NotSchedulerPolicy> pool;
    (void)pool;
    return 0;
}
