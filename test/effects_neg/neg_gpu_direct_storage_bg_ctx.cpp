// NEGATIVE-COMPILE TEST.  This file must fail to compile.
//
// A GPUDirect Storage plan is minted at initialization.  The background
// drain context owns no Init effect, so the mint refuses it.

#include <crucible/cntp/_wip/GpuDirect.h>
#include <fixy/Ctx.h>

namespace cog = crucible::cog;
namespace gd = crucible::cntp::_wip::gpu_direct;

int main() {
    cog::CogIdentity gpu{};
    gpu.uuid = cog::Uuid{1, 2};
    gpu.kind = cog::CogKind::Gpu;
    cog::GpuTargetCaps gpu_caps{};
    gpu_caps.features.set(cog::GpuFeature::GpuDirectStorage);

    cog::CogIdentity nvme{};
    nvme.uuid = cog::Uuid{5, 6};
    nvme.kind = cog::CogKind::NvmeNamespace;

    ::fixy::BgDrainCtx bg{::foundation::effects::testing::bg()};
    auto result = gd::mint_gpu_direct_storage_plan(bg, gpu, gpu_caps, nvme, gd::PeerPlacement{},
                                                   *gd::admit_gpu_virtual_address(0x1000u),
                                                   *gd::admit_gpu_direct_bytes(4096));
    return result.has_value() ? 0 : 1;
}
