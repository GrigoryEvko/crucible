// FIXY-U-083 HS14 neg-compile fixture (1 of 2 for mint_sched_switch).
//
// mint_sched_switch rejects BgDrainCtx — BgDrain carries Bg + Alloc
// but neither engages Init.  SchedSwitch::load() attaches a BPF
// program to the sched_switch tracepoint and mmaps the per-CPU
// histogram, startup-only work that must remain in the Init row.

#include <crucible/perf/SchedSwitch.h>

int main() {
    auto hub = crucible::perf::mint_sched_switch(
        crucible::effects::BgDrainCtx{},
        crucible::effects::testing::init());
    (void)hub;
    return 0;
}
