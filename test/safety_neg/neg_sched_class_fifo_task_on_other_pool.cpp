// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-186 SchedClass, mismatch class #1 of 3:
// POOL-VS-TASK FIT (a SCHED_FIFO task cannot run on a SCHED_OTHER pool).
//
// SchedClass tags a work-item with the scheduler class it expects.  A task
// at `Policy` is runnable on a pool at `PoolPolicy` only when Policy ⊑
// PoolPolicy on the preemption chain.  A SCHED_FIFO task needs at-least-FIFO
// preemption; a SCHED_OTHER pool is too weak (Fifo ⋤ Other), so spawning the
// task on that pool MUST be a compile error — not a runtime priority
// inversion the scheduler silently tolerates.
//
// Distinct from neg_sched_class_deadline_runtime_gt_deadline.cpp (a CBS
// static_assert inside the type) and neg_sched_class_batch_on_hotpath.cpp
// (a HotPath-stance gate); here the failure is the runnable_on<> pool-fit
// constraint at a function call.
//
// Expected diagnostic: constraints not satisfied / no matching function /
// runnable_on / host_on_pool.

#include <crucible/safety/SchedClass.h>

using namespace crucible::safety;

// A SCHED_OTHER thread pool admits only tasks runnable on an Other pool.
template <typename Task>
    requires (Task::template runnable_on<SchedulerPolicy_v::Other>)
[[nodiscard]] int host_on_other_pool(Task task) {
    return task.peek();
}

int main() {
    auto fifo_task = mint_sched_class<SchedulerPolicy_v::Fifo, int>(42);

    // Should FAIL: a SCHED_FIFO task does NOT satisfy runnable_on<Other> —
    // the SCHED_OTHER pool is too weak to host it.
    return host_on_other_pool(fifo_task);
}
