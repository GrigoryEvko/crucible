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

// The default level of each kind follows the groups of the declaration of
// CogKind: an atom is L0, an aggregate of atoms is L1, and a server, a rack,
// a row, a hall and a datacenter have the levels of their names.
static_assert(default_level_for(cog::CogKind::Gpu) == cog::CogLevel::L0_Atomic);
static_assert(default_level_for(cog::CogKind::BmcSensor) == cog::CogLevel::L0_Atomic);
static_assert(default_level_for(cog::CogKind::GpuPackage) == cog::CogLevel::L1_Component);
static_assert(default_level_for(cog::CogKind::CpuSocket) == cog::CogLevel::L1_Component);
static_assert(default_level_for(cog::CogKind::RackPsu) == cog::CogLevel::L1_Component);
static_assert(default_level_for(cog::CogKind::NicCard) == cog::CogLevel::L1_Component);
static_assert(default_level_for(cog::CogKind::Server) == cog::CogLevel::L3_Chassis);
static_assert(default_level_for(cog::CogKind::Rack) == cog::CogLevel::L4_Rack);
static_assert(default_level_for(cog::CogKind::Row) == cog::CogLevel::L5_Row);
static_assert(default_level_for(cog::CogKind::Hall) == cog::CogLevel::L6_Hall);
static_assert(default_level_for(cog::CogKind::Datacenter) == cog::CogLevel::L7_Datacenter);

}  // namespace crucible::topology
