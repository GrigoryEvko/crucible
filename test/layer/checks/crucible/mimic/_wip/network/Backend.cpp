// The compile-time checks of crucible/mimic/_wip/network/Backend.h.

#include <crucible/mimic/_wip/network/Backend.h>

namespace crucible::mimic::_wip::network {

static_assert(std::is_trivially_copyable_v<NetworkKernelArtifact>);

static_assert(sizeof(DeclaredNetworkKernel<NetworkBackendVendor::Cpu>) == sizeof(NetworkKernelArtifact));
static_assert(NetworkBackendCanPlan<NetworkBackendVendor::Cpu, cog::CogKind::CpuSocket, ir::AllReduceOp>);
static_assert(NetworkBackendCanPlan<NetworkBackendVendor::Nv, cog::CogKind::Gpu, ir::SendOp>);
static_assert(!BackendAcceptsCog<NetworkBackendVendor::Mellanox, cog::CogKind::Gpu>);

}  // namespace crucible::mimic::_wip::network
