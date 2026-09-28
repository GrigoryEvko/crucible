// A SCHED_DEADLINE class states its CBS budget in the type, and the kernel
// admits it only when RuntimeNs < DeadlineNs <= PeriodNs.  A runtime of
// 100 ns against a deadline of 50 ns fails the class-body static_assert.
#include <fixy/os/SchedClass.h>

static_assert(sizeof(fixy::SchedClass<fixy::SchedulerPolicy_v::Deadline, int, 100, 50, 200>) > 0);

int main() { return 0; }
