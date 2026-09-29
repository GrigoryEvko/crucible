// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The gate of mint_sense_hub asks for a context row that contains Alloc,
// IO and Block, because the load enters the kernel and waits for the
// verifier.  BgDrainCtx claims Row<Bg, Alloc>, so the gate rejects it.
// The second argument is a valid startup load context, so the gate is
// the one reason that the compiler rejects the call.  The hot foreground
// fixture of this pair fails the same gate from an empty row.

#include <crucible/perf/SenseHub.h>
#include <fixy/Ctx.h>

namespace neg_fixy_v_179_perf_hubs_bg_drain {

[[maybe_unused]] constexpr auto bad_dispatch =
    ::crucible::perf::mint_sense_hub(::fixy::BgDrainCtx{::foundation::effects::testing::bg()},
                                     ::fixy::InitLoadCtx{::foundation::effects::testing::init()});

}  // namespace neg_fixy_v_179_perf_hubs_bg_drain

int main() { return 0; }
