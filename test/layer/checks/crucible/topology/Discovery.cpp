// The compile-time checks of crucible/topology/Discovery.h.

#include <crucible/topology/Discovery.h>

namespace crucible::topology {

static_assert(DiscoveryReport::max_statuses <= std::numeric_limits<std::uint8_t>::max(),
              "DiscoveryReport::size_ must be able to name every slot");

static_assert(sizeof(ExternalDiscoveryText) == sizeof(std::string_view));
static_assert(sizeof(VendorDiscoveryString) == sizeof(std::string_view));
static_assert(DiscoveryShape<1, 1>);
static_assert(!DiscoveryShape<0, 1>);
static_assert(std::is_trivially_destructible_v<DiscoveryNodeFact>);
static_assert(std::is_trivially_destructible_v<DiscoveryEdgeFact>);
static_assert(!std::is_default_constructible_v<DefaultDiscoverySnapshot>,
              "a snapshot is built only by mint_discovery_snapshot");
static_assert(CtxFitsDiscoveryInit<::fixy::ColdInitCtx>);
static_assert(!CtxFitsDiscoveryInit<::fixy::BgDrainCtx>);
static_assert(CtxFitsDiscoveryBg<::fixy::BgDrainCtx>);
static_assert(!CtxFitsDiscoveryBg<::fixy::ColdInitCtx>);

}  // namespace crucible::topology
