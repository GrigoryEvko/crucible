// The compile-time checks of crucible/cntp/BackpressureRuntime.h.

#include <crucible/cntp/BackpressureRuntime.h>

namespace crucible::cntp {

static_assert(CtxFitsBackpressureMint<::fixy::ColdInitCtx>);
static_assert(!CtxFitsBackpressureMint<::fixy::BgDrainCtx>);
static_assert(CtxFitsBackpressureRuntime<::fixy::BgDrainCtx>);
static_assert(CtxFitsBackpressureRuntime<::fixy::TestRunnerCtx>);
static_assert(!CtxFitsBackpressureRuntime<::fixy::HotFgCtx>);
static_assert(CtxFitsBackpressureStart<::fixy::BgLoadCtx>);
static_assert(CtxFitsBackpressureStart<::fixy::TestRunnerCtx>);
static_assert(!CtxFitsBackpressureStart<::fixy::BgDrainCtx>,
              "a context that owns no Block cannot wait on the start gate");

}  // namespace crucible::cntp
