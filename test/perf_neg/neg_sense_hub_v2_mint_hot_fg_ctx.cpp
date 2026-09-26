// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_sense_hub_v2 rejects HotFgCtx.  The hot foreground context must
// never pay the multi-millisecond BPF program load and mmap.  This is a
// distinct mismatch class from the BgDrainCtx fixture: HotFgCtx claims
// the empty row, so it carries none of the three atoms the gate demands.

#include <crucible/perf/SenseHubV2.h>
#include <fixy/Ctx.h>

int main() {
    auto hub = crucible::perf::mint_sense_hub_v2(::foundation::effects::testing::foreground(),
                                                 ::fixy::InitLoadCtx{::foundation::effects::testing::init()});
    (void)hub;
    return 0;
}
