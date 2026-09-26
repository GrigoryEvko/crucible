#include <crucible/observe/SdcDetect.h>

namespace crucible::observe {

static_assert(CtxFitsSdcMint<::fixy::ColdInitCtx>);
static_assert(CtxFitsSdcRun<::fixy::BgDrainCtx>);

}  // namespace crucible::observe
