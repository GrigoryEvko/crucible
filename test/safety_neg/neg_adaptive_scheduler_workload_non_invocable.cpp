// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// dispatch_with_workload accepts either a plain void() job or a
// void(WorkShard) sharded job.  Arbitrary payload objects must be
// rejected at the scheduler boundary before they reach queue storage.

#include <crucible/concurrent/AdaptiveScheduler.h>

struct NotAJob {
    int payload = 0;
};

int main() {
    namespace cc = crucible::concurrent;
    namespace cs = crucible::concurrent::scheduler;

    cc::Pool<cs::Fifo> pool{cc::CoreCount{1}};
    const auto profile = cc::WorkloadProfile::from_budget(
        cc::WorkBudget{.read_bytes = 64, .write_bytes = 64, .item_count = 8});
    (void)cc::dispatch_with_workload(pool, profile, NotAJob{});
    return 0;
}
