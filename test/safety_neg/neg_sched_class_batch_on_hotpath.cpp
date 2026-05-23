// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-186 SchedClass, mismatch class #3 of 3:
// HOTPATH STANCE REJECTS SCHED_BATCH (background-only work).
//
// SCHED_BATCH (and SCHED_IDLE) are non-interactive background classes — the
// kernel deliberately denies them the interactive preemption boost.  A
// HotPath stance therefore requires AT LEAST Other-strength scheduling
// (leq(Other, Policy) on the preemption chain); Batch (⊏ Other) is rejected.
// Pinning a SCHED_BATCH task to a hot-path consumer would silently starve a
// latency-critical code path behind throughput-oriented background work.
//
// Distinct from neg_sched_class_fifo_task_on_other_pool.cpp (a runnable_on
// pool-fit constraint) and neg_sched_class_deadline_runtime_gt_deadline.cpp
// (a CBS static_assert); here the failure is a HotPath-eligibility gate
// (the leq(Other, policy) threshold) at a function call.
//
// Expected diagnostic: constraints not satisfied / no matching function /
// hot_path_eligible / on_hot_path.

#include <crucible/safety/SchedClass.h>

#include <crucible/algebra/lattices/SchedulerPolicyLattice.h>

using namespace crucible::safety;

// A hot-path consumer admits only at-least-Other-strength scheduler classes
// (the V-183 CtxFitsTscReader threshold: leq(Other, policy)).
template <typename Task>
    requires (::crucible::algebra::lattices::SchedulerPolicyLattice::leq(
                  SchedulerPolicy_v::Other, Task::policy))
[[nodiscard]] int on_hot_path(Task task) {
    return task.peek();
}

int main() {
    auto batch_task = mint_sched_class<SchedulerPolicy_v::Batch, int>(42);

    // Should FAIL: SCHED_BATCH (⊏ Other) is non-interactive background work
    // and does NOT satisfy the hot-path stance's leq(Other, policy) gate.
    return on_hot_path(batch_task);
}
