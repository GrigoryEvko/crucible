// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: trait accessor requiring SchedulerPolicy concept called
// on a type that does NOT inherit from policy_base.  The concept's
// requires-clause rejects.

#include <crucible/concurrent/SchedulerPolicy.h>

using namespace crucible::concurrent::scheduler;

struct NotAPolicy {};  // missing : policy_base

int main() {
    // scheduler_name_v requires SchedulerPolicy<T>.
    constexpr auto n = scheduler_name_v<NotAPolicy>;
    (void)n;
    return 0;
}
