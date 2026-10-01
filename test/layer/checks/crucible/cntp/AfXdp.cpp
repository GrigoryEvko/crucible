// The compile-time checks of crucible/cntp/AfXdp.h.

#include <crucible/cntp/AfXdp.h>

namespace crucible::cntp {

static_assert(sizeof(AfXdpIfIndex) == sizeof(std::uint32_t));
static_assert(sizeof(AfXdpQueueId) == sizeof(std::uint32_t));
static_assert(sizeof(AfXdpFrameSize) == sizeof(std::uint32_t));
static_assert(sizeof(AfXdpFrameCount) == sizeof(std::uint32_t));
static_assert(sizeof(AfXdpRingEntries) == sizeof(std::uint32_t));
static_assert(sizeof(DeclaredAfXdpConfig) == sizeof(AfXdpConfig));
// A refined member makes the config not trivially copyable, because no byte
// route may build a refined value.  A copy still costs what copying the
// bytes costs.
static_assert(std::is_trivially_copy_constructible_v<AfXdpConfig> && std::is_trivially_destructible_v<AfXdpConfig>);
static_assert(AfXdpStaticShape<131072, 2048, 64, 64, 64, 64>);
static_assert(!AfXdpStaticShape<131072, 1500, 64, 64, 64, 64>);
static_assert(CtxFitsAfXdpMint<::fixy::ColdInitCtx>);
static_assert(!CtxFitsAfXdpMint<::fixy::BgDrainCtx>);
static_assert(!CtxFitsAfXdpMint<::fixy::HotFgCtx>);

}  // namespace crucible::cntp
