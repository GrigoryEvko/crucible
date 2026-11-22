// The compile-time checks of crucible/topology/Discovery.h.

#include <crucible/topology/Discovery.h>

#include <fixy/Core.h>

namespace crucible::topology {

static_assert(DiscoveryReport::max_statuses <= ::fixy::max_of<std::uint8_t>,
              "DiscoveryReport::size_ must be able to name every slot");

static_assert(sizeof(ExternalDiscoveryText) == sizeof(::fixy::TextView));
static_assert(sizeof(VendorDiscoveryString) == sizeof(VendorDiscoveryText), "the tag adds no byte to a vendor text");
static_assert(vendor_discovery_text_capacity % 8 == 0, "a vendor text holds no padding byte");
static_assert(::foundation::core::HoldsNoBorrow<VendorDiscoveryString>,
              "a node fact owns each of its texts, so it outlives the text of its tool");
static_assert(!std::is_copy_constructible_v<DefaultDiscoverySnapshot>
                  && !std::is_move_constructible_v<DefaultDiscoverySnapshot>,
              "each identity views the texts of the facts of its snapshot, so a snapshot never copies or moves");
static_assert(!std::is_copy_assignable_v<DefaultDiscoverySnapshot>
              && !std::is_move_assignable_v<DefaultDiscoverySnapshot>);
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
