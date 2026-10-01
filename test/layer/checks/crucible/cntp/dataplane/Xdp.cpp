// The compile-time checks of crucible/cntp/dataplane/Xdp.h.

#include <crucible/cntp/dataplane/Xdp.h>

namespace crucible::cntp::dataplane {

static_assert(static_cast<std::uint8_t>(XdpAction::Pass) == 2);
static_assert(sizeof(XdpIfIndex) == sizeof(std::uint32_t));
static_assert(sizeof(PositiveMapEntries) == sizeof(std::uint32_t));
static_assert(sizeof(PositiveMapElementBytes) == sizeof(std::uint16_t));
static_assert(sizeof(DeclaredXdpProgram) == sizeof(XdpProgramSpec));
static_assert(sizeof(DeclaredBpfMap) == sizeof(BpfMapSpec));
// A refined member keeps a spec from being trivially copyable, by design: no
// byte copy may build a positive value.  Copy construction stays trivial.
static_assert(std::is_trivially_copy_constructible_v<XdpProgramSpec>);
static_assert(std::is_trivially_destructible_v<XdpProgramSpec>);
static_assert(std::is_trivially_copy_constructible_v<BpfMapSpec>);
static_assert(std::is_trivially_destructible_v<BpfMapSpec>);

}  // namespace crucible::cntp::dataplane
