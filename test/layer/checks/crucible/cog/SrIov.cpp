// The compile-time checks of crucible/cog/SrIov.h.

#include <crucible/cog/SrIov.h>

namespace crucible::cog::sriov {

static_assert(sizeof(VfCount) == sizeof(std::uint16_t));
static_assert(sizeof(VfIndex) == sizeof(std::uint16_t));
static_assert(sizeof(VfVlanId) == sizeof(std::uint16_t));
static_assert(sizeof(VfRateLimitMbps) == sizeof(std::uint64_t));
static_assert(sizeof(VfResourceLimit) == sizeof(std::uint32_t));
static_assert(sizeof(VfMacAddress) == sizeof(MacAddress));
static_assert(sizeof(DeclaredSrIovPlan) == sizeof(SrIovPlan));
static_assert(CtxFitsSrIovMint<::fixy::ColdInitCtx>);
static_assert(!CtxFitsSrIovMint<::fixy::BgDrainCtx>);
static_assert(!CtxFitsSrIovMint<::fixy::HotFgCtx>);
static_assert(std::is_trivially_copyable_v<MacAddress>);
static_assert(!std::is_constructible_v<VfHandle, CogIdentity, VfIndex>, "a handle is built only from a declared plan");

// A refined field keeps a plan cheap to copy and free to destroy. No
// byte copy builds a refined field, so a plan is not trivially copyable.
static_assert(std::is_trivially_copy_constructible_v<VfConfig> && std::is_trivially_destructible_v<VfConfig>);
static_assert(std::is_trivially_copy_constructible_v<SrIovPlan> && std::is_trivially_destructible_v<SrIovPlan>);
static_assert(std::is_trivially_copy_constructible_v<VfHandle> && std::is_trivially_destructible_v<VfHandle>);
static_assert(!std::is_trivially_copyable_v<VfHandle>);

}  // namespace crucible::cog::sriov
