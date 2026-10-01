// The compile-time checks of crucible/topology/AsymmetricFailure.h.

#include <crucible/topology/AsymmetricFailure.h>

namespace crucible::topology {

static_assert(::foundation::diag::is_diagnostic_class_v<AsymmetricFailureDetected>);
static_assert(sizeof(DirectionWindow) == 6);
static_assert(std::is_trivially_copyable_v<FailureSummary>);
static_assert(std::is_trivially_copyable_v<AsymmetricFailureEvent>);
static_assert(sizeof(AsymmetricFailureEvent) <= 64);
static_assert(!std::is_constructible_v<AsymmetricFailureDetector<1, 1, 1>, AsymmetricFailurePolicy>,
              "the detector is reached only through mint_asymmetric_failure_detector");
static_assert(CtxFitsAsymmetricFailureMint<::fixy::ColdInitCtx>);
static_assert(!CtxFitsAsymmetricFailureMint<::fixy::BgDrainCtx>);
static_assert(CtxFitsAsymmetricFailureRecord<::fixy::BgDrainCtx>);
static_assert(!CtxFitsAsymmetricFailureRecord<::fixy::HotFgCtx>);

}  // namespace crucible::topology
