// The compile-time checks of crucible/cog/NicConfig.h.

#include <crucible/cog/NicConfig.h>

namespace crucible::cog::nic {

static_assert(sizeof(NicRingSize) == sizeof(std::uint16_t));
static_assert(sizeof(NicQueueCount) == sizeof(std::uint16_t));
static_assert(sizeof(SysctlBytes) == sizeof(std::uint64_t));
static_assert(sizeof(BusyPollUs) == sizeof(std::uint32_t));
static_assert(sizeof(TcpRtoMinUs) == sizeof(std::uint32_t));
static_assert(sizeof(DeclaredNicConfig) == sizeof(NicConfigPlan));
static_assert(CtxFitsNicConfigMint<::fixy::ColdInitCtx>);
static_assert(!CtxFitsNicConfigMint<::fixy::BgDrainCtx>);
static_assert(!CtxFitsNicConfigMint<::fixy::HotFgCtx>);

// A refined field keeps a configuration cheap to copy and free to
// destroy. No byte copy builds a refined field, so a configuration is
// not trivially copyable.
static_assert(std::is_trivially_copy_constructible_v<EthtoolConfig> && std::is_trivially_destructible_v<EthtoolConfig>);
static_assert(std::is_trivially_copy_constructible_v<QdiscConfig> && std::is_trivially_destructible_v<QdiscConfig>);
static_assert(std::is_trivially_copy_constructible_v<SysctlConfig> && std::is_trivially_destructible_v<SysctlConfig>);
static_assert(!std::is_trivially_copyable_v<SysctlConfig>);

}  // namespace crucible::cog::nic
