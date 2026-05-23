// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-186 SchedClass, mismatch class #2 of 3:
// SCHED_DEADLINE CBS-ADMISSION ASSERT (runtime must be < deadline).
//
// SCHED_DEADLINE is CBS-admitted EDF: the kernel admits a task only when
// `runtime < deadline <= period`.  The DEADLINE case of SchedClass carries
// the three budgets as NTTPs and asserts the admission inequality at
// COMPILE time, so an inadmissible budget is a build error rather than a
// runtime sched_setattr(EINVAL).  Here RuntimeNs (100) > DeadlineNs (50)
// violates `runtime < deadline`, so instantiating the type fires the
// static_assert.
//
// Distinct from neg_sched_class_fifo_task_on_other_pool.cpp (a runnable_on
// constraint) and neg_sched_class_batch_on_hotpath.cpp (a HotPath gate);
// here the failure is a static_assert INSIDE the wrapper type at
// instantiation.
//
// Expected diagnostic: static assertion failed / SCHED_DEADLINE requires /
// CBS admission.

#include <crucible/safety/SchedClass.h>

using namespace crucible::safety;

int main() {
    // Should FAIL: RuntimeNs=100 > DeadlineNs=50 violates the CBS admission
    // inequality runtime < deadline <= period; the static_assert in
    // SchedClass<Deadline, ...> fires at instantiation.
    auto bad = mint_sched_class<SchedulerPolicy_v::Deadline, int, 100, 50, 200>(7);

    return bad.peek();
}
