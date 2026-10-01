// The compile-time checks of crucible/observe/SdcDetect.h.

#include <crucible/observe/SdcDetect.h>

namespace crucible::observe {

// A refined field keeps no byte route into it, so the config is not
// trivially copyable.  Its copies stay trivial, so a config still passes
// by value in registers.
static_assert(std::is_trivially_copy_constructible_v<SdcConfig> && std::is_trivially_destructible_v<SdcConfig>);
static_assert(std::is_trivially_copyable_v<SdcEvent>);

static_assert(CtxFitsSdcMint<::fixy::ColdInitCtx>);
static_assert(!CtxFitsSdcMint<::fixy::BgDrainCtx>);
static_assert(CtxFitsSdcRun<::fixy::BgDrainCtx>);
static_assert(!CtxFitsSdcRun<::fixy::HotFgCtx>);
static_assert(!std::is_constructible_v<SdcDetector<1, 1>, SdcConfig>);

}  // namespace crucible::observe
