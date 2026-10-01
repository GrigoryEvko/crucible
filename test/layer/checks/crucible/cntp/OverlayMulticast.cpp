// The compile-time checks of crucible/cntp/OverlayMulticast.h.

#include <crucible/cntp/OverlayMulticast.h>

namespace crucible::cntp {

static_assert(sizeof(OverlayStripeCount) == sizeof(std::uint8_t));
static_assert(sizeof(OverlayRecoveryThreshold) == sizeof(std::uint8_t));
static_assert(sizeof(OverlayFanout) == sizeof(std::uint8_t));
static_assert(sizeof(OverlayPayloadBytes) == sizeof(std::uint32_t));
static_assert(sizeof(DeclaredOverlayPeer) == sizeof(OverlayPeerRef));
static_assert(std::is_trivially_copyable_v<OverlayPeerRef>);
// Refined members keep the config from being trivially copyable, by design:
// no byte copy may build a bounded value.  Copy construction stays trivial.
static_assert(std::is_trivially_copy_constructible_v<OverlayMulticastConfig>);
static_assert(std::is_trivially_destructible_v<OverlayMulticastConfig>);
static_assert(OverlayMulticastShape<8, 8, 2>);
static_assert(!OverlayMulticastShape<0, 8, 2>);
static_assert(!OverlayMulticastShape<8, 0, 2>);
static_assert(!OverlayMulticastShape<8, 8, 0>);
static_assert(!OverlayMulticastShape<8, kOverlayMaxStripes + 1u, 2>);

}  // namespace crucible::cntp
