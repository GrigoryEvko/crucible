// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// HS14 fixture for GAPS-132. GPUDirect MR intent is startup resource
// registration work and requires an Init-row context.

#include <crucible/cntp/GpuDirect.h>

namespace cog = crucible::cog;
namespace eff = crucible::effects;
namespace gd = crucible::cntp::gpu_direct;

int main() {
    cog::CogIdentity gpu{};
    gpu.uuid = cog::Uuid{1, 2};
    gpu.kind = cog::CogKind::Gpu;
    cog::GpuTargetCaps gpu_caps{};
    gpu_caps.features.set(cog::GpuFeature::GpuDirectRdma);

    cog::CogIdentity nic{};
    nic.uuid = cog::Uuid{3, 4};
    nic.kind = cog::CogKind::NicPort;
    cog::NicPortTargetCaps nic_caps{};
    nic_caps.features.set(cog::NicFeature::GpuDirectRdma);

    auto result = gd::mint_gpu_direct_mr_plan(
        eff::BgDrainCtx{}, gpu, gpu_caps, nic, nic_caps,
        gd::PeerPlacement{}, *gd::admit_gpu_virtual_address(0x1000u),
        *gd::admit_gpu_direct_bytes(4096));
    return result.has_value() ? 0 : 1;
}
