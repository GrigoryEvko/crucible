// The compile-time checks of crucible/cog/Calibrate.h.

#include <crucible/cog/Calibrate.h>

namespace crucible::cog {

static_assert(sizeof(CalibrationIterations) == sizeof(std::uint32_t));
static_assert(sizeof(WarmupIterations) == sizeof(std::uint32_t));
static_assert(sizeof(TrimBasisPoints) == sizeof(std::uint16_t));
static_assert(sizeof(RuntimeBudgetMs) == sizeof(std::uint32_t));
static_assert(sizeof(CalibrationSampleCount) == sizeof(std::uint16_t));
static_assert(sizeof(CalibrationLatencyQuantiles) == sizeof(LatencyQuantiles));
static_assert(sizeof(CalibratedThroughput) == sizeof(double));
static_assert(CalibratableCogKind<CogKind::Gpu>);
static_assert(CalibratableCogKind<CogKind::NicPort>);
static_assert(!CalibratableCogKind<CogKind::PsuRail>);
static_assert(CtxFitsCalibration<::fixy::ColdInitCtx>);
static_assert(CtxFitsCalibration<::fixy::BgDrainCtx>);
static_assert(!CtxFitsCalibration<::fixy::HotFgCtx>);
static_assert(!CtxFitsCalibration<::fixy::TestRunnerCtx>);

// A refined member keeps the class from being trivially copyable, by
// design: no byte copy builds a refined value. A plan and a drift signal
// still copy and destroy as plain data.
static_assert(std::is_trivially_copy_constructible_v<CalibrationPlan>
              && std::is_trivially_destructible_v<CalibrationPlan>);
static_assert(std::is_trivially_copy_constructible_v<DriftSignal> && std::is_trivially_destructible_v<DriftSignal>);

}  // namespace crucible::cog
