// The compile-time checks of crucible/topology/Health.h.

#include <crucible/topology/Health.h>

namespace crucible::topology {

static_assert(sizeof(PhiMilli) == sizeof(std::uint32_t));

static_assert(sizeof(HealthScore) == sizeof(std::uint16_t));

static_assert(::foundation::diag::is_diagnostic_class_v<Health_Degraded>);
static_assert(std::is_trivially_copyable_v<HealthSnapshot>);
static_assert(std::is_trivially_destructible_v<HealthSnapshot>);
static_assert(sizeof(HealthDeltaEvent) <= 64);
static_assert(detail::monotone_count_members<EccCounters>() == 2);
static_assert(detail::monotone_count_members<DropCounters>() == 5);
static_assert(!std::is_constructible_v<CompositeHealthScorer<1, 2, 1>, HealthPolicy>,
              "the scorer is reached only through mint_topology_health");
static_assert(CtxFitsHealthMint<::fixy::ColdInitCtx>);
static_assert(!CtxFitsHealthMint<::fixy::BgDrainCtx>);
static_assert(CtxFitsHealthUpdate<::fixy::BgDrainCtx>);
static_assert(!CtxFitsHealthUpdate<::fixy::HotFgCtx>);

}  // namespace crucible::topology
