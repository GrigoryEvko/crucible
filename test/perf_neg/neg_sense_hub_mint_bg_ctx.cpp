// FIXY-U-083 HS14 neg-compile fixture (1 of 2 for mint_sense_hub).
//
// mint_sense_hub rejects a context whose effect row lacks Init.
// BgDrainCtx::row = Row<Bg, Alloc> — no Init grant.  SenseHub::load()
// performs BPF program loading + tracepoint attach via bpf() and
// perf_event_open syscalls plus mmap — startup-only operations
// belonging to the Init row.  The Bg drain context must NOT engage
// this surface; the Ctx-fit gate refuses to satisfy the requires
// clause for BgDrainCtx.

#include <crucible/perf/SenseHub.h>

int main() {
    auto hub = crucible::perf::mint_sense_hub(
        crucible::effects::BgDrainCtx{},
        crucible::effects::testing::init());
    (void)hub;
    return 0;
}
