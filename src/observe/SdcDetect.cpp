#include <crucible/observe/SdcDetect.h>

namespace crucible::observe {

static_assert(CtxFitsSdcMint<effects::ColdInitCtx>);
static_assert(CtxFitsSdcRun<effects::BgDrainCtx>);

}  // namespace crucible::observe
