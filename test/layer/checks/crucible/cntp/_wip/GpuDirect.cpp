// The compile-time checks of crucible/cntp/_wip/GpuDirect.h.

#include <crucible/cntp/_wip/GpuDirect.h>

namespace crucible::cntp::_wip::gpu_direct {

static_assert(sizeof(GpuVirtualAddress) == sizeof(std::uintptr_t));
static_assert(sizeof(GpuDirectByteCount) == sizeof(std::uint64_t));
static_assert(sizeof(PcieRootId) == sizeof(std::uint16_t));
static_assert(sizeof(DeclaredGpuDirectMrPlan) == sizeof(GpuDirectMrPlan));
static_assert(sizeof(DeclaredGpuDirectStoragePlan) == sizeof(GpuDirectStoragePlan));
static_assert(::fixy::qtt_consume_tracked || sizeof(OwnedGpuDirectMr) == sizeof(GpuDirectMrHandle));
static_assert(CtxFitsGpuDirectMint<::fixy::ColdInitCtx>);
static_assert(!CtxFitsGpuDirectMint<::fixy::BgDrainCtx>);
static_assert(std::is_trivially_copyable_v<PeerPlacement>);
// A refined member makes a plan not trivially copyable, because no byte
// route may build a refined value.  A copy still costs what copying the
// bytes costs.
static_assert(std::is_trivially_copy_constructible_v<GpuDirectMrPlan>
              && std::is_trivially_destructible_v<GpuDirectMrPlan>);
static_assert(std::is_trivially_copy_constructible_v<GpuDirectStoragePlan>
              && std::is_trivially_destructible_v<GpuDirectStoragePlan>);
// No declared plan exists before its address and byte count, and no region
// handle exists outside the registry or beside the one it names.
static_assert(!std::is_default_constructible_v<DeclaredGpuDirectMrPlan>);
static_assert(!std::is_default_constructible_v<DeclaredGpuDirectStoragePlan>);
static_assert(
    !std::is_constructible_v<GpuDirectMrHandle, cog::Uuid, cog::Uuid, GpuVirtualAddress, GpuDirectByteCount, MrAccess>);
static_assert(!std::is_copy_constructible_v<GpuDirectMrHandle>
              && std::is_nothrow_move_constructible_v<GpuDirectMrHandle>);

}  // namespace crucible::cntp::_wip::gpu_direct
