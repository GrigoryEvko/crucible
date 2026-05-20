// FIXY-U-083 HS14 neg-compile fixture (1 of 2 for mint_lock_contention).
//
// mint_lock_contention rejects BgDrainCtx — BgDrain carries Bg + Alloc
// capabilities but neither engages Init.  LockContention::load() opens
// a BPF program + tracepoint and mmaps the per-CPU histogram, which is
// startup-only work that must remain in the Init row.

#include <crucible/perf/LockContention.h>

int main() {
    auto hub = crucible::perf::mint_lock_contention(
        crucible::effects::BgDrainCtx{},
        crucible::effects::testing::init());
    (void)hub;
    return 0;
}
