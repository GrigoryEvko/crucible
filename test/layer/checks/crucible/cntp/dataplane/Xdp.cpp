// The compile-time checks of crucible/cntp/dataplane/Xdp.h.

#include <crucible/cntp/dataplane/Xdp.h>
#include <fixy/Ctx.h>

#include <type_traits>

namespace crucible::cntp::dataplane {

static_assert(static_cast<std::uint8_t>(XdpAction::Pass) == 2);
static_assert(static_cast<std::uint8_t>(XdpAction::Redirect) == 4);
static_assert(static_cast<std::uint8_t>(BpfMapUpdate::Exist) == 2);
static_assert(sizeof(XdpIfIndex) == sizeof(std::uint32_t));
static_assert(sizeof(PositiveMapEntries) == sizeof(std::uint32_t));
static_assert(sizeof(PositiveMapElementBytes) == sizeof(std::uint16_t));
static_assert(sizeof(DeclaredXdpProgram) == sizeof(XdpProgramSpec));
static_assert(sizeof(DeclaredBpfMap) == sizeof(BpfMapSpec));
// A refined member keeps a spec from being trivially copyable: no byte copy
// builds a positive value.  The copy construction stays trivial.
static_assert(std::is_trivially_copy_constructible_v<XdpProgramSpec>);
static_assert(std::is_trivially_destructible_v<XdpProgramSpec>);
static_assert(std::is_trivially_copy_constructible_v<BpfMapSpec>);
static_assert(std::is_trivially_destructible_v<BpfMapSpec>);
static_assert(CtxFitsXdpMint<::fixy::ColdInitCtx>);
static_assert(!CtxFitsXdpMint<::fixy::BgDrainCtx>);
static_assert(BpfMapElementWidth<::fixy::FixedArray<std::byte, 65535>>);
static_assert(!BpfMapElementWidth<::fixy::FixedArray<std::byte, 65536>>);
static_assert(admit_xdp_ifindex(0).is_err());
static_assert(admit_xdp_ifindex(1).expect("1 is an interface index").value() == 1u);
static_assert(xdp_required_features(XdpMode::Generic) == ::fixy::Bits<cog::NicFeature>{});
static_assert(xdp_action_name(XdpAction::Tx) == "Tx");

}  // namespace crucible::cntp::dataplane
