// NEGATIVE-COMPILE TEST.  This file must fail to compile.
//
// A GPUDirect memory-region plan is minted at initialization.  The
// foreground context claims no effect, so the mint refuses it.

#include <crucible/cntp/_wip/GpuDirect.h>
#include <fixy/Ctx.h>

namespace cog = crucible::cog;
namespace gd = crucible::cntp::_wip::gpu_direct;

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

    const ::fixy::HotFgCtx hot = ::foundation::effects::testing::foreground();
    auto result = gd::mint_gpu_direct_mr_plan(hot, gpu, gpu_caps, nic, nic_caps, gd::PeerPlacement{},
                                              *gd::admit_gpu_virtual_address(0x1000u),
                                              *gd::admit_gpu_direct_bytes(4096));
    return result.has_value() ? 0 : 1;
}
