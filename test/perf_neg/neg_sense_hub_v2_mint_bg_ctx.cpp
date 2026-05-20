// FIXY-U-083 HS14 neg-compile fixture (1 of 2 for mint_sense_hub_v2).
//
// mint_sense_hub_v2 rejects BgDrainCtx — same Ctx-fit gate as the v1
// SenseHub.  The v2 surface adds telemetry deltas and full snapshots
// on top of v1, but the load-path syscall + mmap remain Init-row
// concerns; bg drain frames must never engage.

#include <crucible/perf/SenseHubV2.h>

int main() {
    auto hub = crucible::perf::mint_sense_hub_v2(
        crucible::effects::BgDrainCtx{},
        crucible::effects::testing::init());
    (void)hub;
    return 0;
}
