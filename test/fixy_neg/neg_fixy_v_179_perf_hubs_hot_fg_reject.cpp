// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The gate of mint_pmu_sample asks for a context row that contains
// Alloc, IO and Block, because the load enters the kernel and waits for
// the verifier.  HotFgCtx claims the empty row, so the gate rejects it,
// and the hot path never waits for a program load.  The second argument
// is a valid startup load context, so the gate is the one reason that
// the compiler rejects the call.  The background drain fixture of this
// pair fails the same gate from a different row.

#include <crucible/perf/PmuSample.h>
#include <fixy/Ctx.h>

namespace neg_fixy_v_179_perf_hubs_hot_fg {

[[maybe_unused]] constexpr auto bad_dispatch = ::crucible::perf::mint_pmu_sample(
    ::foundation::effects::testing::foreground(), ::fixy::InitLoadCtx{::foundation::effects::testing::init()});

}  // namespace neg_fixy_v_179_perf_hubs_hot_fg

int main() { return 0; }
