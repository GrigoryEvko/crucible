// The compile-time checks of crucible/mimic/Fence.h.

#include <crucible/mimic/Fence.h>

namespace crucible::mimic {

static_assert(sizeof(FenceSpec) == 3, "FenceSpec is three uint8_t fields, no padding");
static_assert(alignof(FenceSpec) == 1);

namespace detail::fence_lowering_self_test {

using BS = BarrierStrength;
using MS = MemoryScope;
using FA = FenceArch;

static_assert(lower_fence<BS::None, MS::System, FA::X86>().kind == FenceKind::NoOp);
static_assert(lower_fence<BS::CompilerBarrier, MS::System, FA::Arm>().kind == FenceKind::CompilerBarrier);

static_assert(lower_fence<BS::AcqRel, MS::System, FA::X86>().kind == FenceKind::CompilerBarrier);
static_assert(lower_fence<BS::SeqCst, MS::System, FA::X86>().kind == FenceKind::X86Mfence);
static_assert(lower_fence<BS::FullFence, MS::System, FA::X86>().kind == FenceKind::X86Mfence);

static_assert(lower_fence<BS::AcqRel, MS::Inner, FA::Arm>()
              == FenceSpec{FenceKind::ArmDmb, FenceDomain::ArmIsh, FenceOrder::AcqRel});
static_assert(lower_fence<BS::AcquireLoad, MS::Inner, FA::Arm>().order == FenceOrder::Acquire);
static_assert(lower_fence<BS::ReleaseStore, MS::Outer, FA::Arm>()
              == FenceSpec{FenceKind::ArmDmb, FenceDomain::ArmOsh, FenceOrder::Release});
static_assert(lower_fence<BS::SeqCst, MS::System, FA::Arm>().domain == FenceDomain::ArmSy);
static_assert(lower_fence<BS::AcqRel, MS::Thread, FA::Arm>().kind == FenceKind::CompilerBarrier);

static_assert(lower_fence<BS::AcqRel, MS::Cta, FA::Gpu>()
              == FenceSpec{FenceKind::GpuFence, FenceDomain::GpuCta, FenceOrder::AcqRel});
static_assert(lower_fence<BS::SeqCst, MS::Gpu, FA::Gpu>()
              == FenceSpec{FenceKind::GpuFence, FenceDomain::GpuGpu, FenceOrder::SeqCst});
static_assert(lower_fence<BS::AcqRel, MS::Cluster, FA::Gpu>().domain == FenceDomain::GpuCluster);
static_assert(lower_fence<BS::AcqRel, MS::Warp, FA::Gpu>().kind == FenceKind::CompilerBarrier);

static_assert(fence_mnemonic(lower_fence<BS::SeqCst, MS::System, FA::X86>())[0] == 'm');
static_assert(fence_mnemonic(lower_fence<BS::AcqRel, MS::Inner, FA::Arm>())[4] == 'i');
static_assert(fence_mnemonic(lower_fence<BS::SeqCst, MS::Gpu, FA::Gpu>())[6] == 's');
static_assert(fence_mnemonic(FenceSpec{})[0] == '\0');

static_assert(fence_arch_scope_consistent(FA::Gpu, MS::Cta));
static_assert(!fence_arch_scope_consistent(FA::X86, MS::Cta));
static_assert(!fence_arch_scope_consistent(FA::Arm, MS::Gpu));
static_assert(!fence_arch_scope_consistent(FA::Gpu, MS::Inner));
static_assert(fence_arch_scope_consistent(FA::X86, MS::System));
static_assert(fence_strength_meets_scope(BS::AcqRel, MS::Gpu));
static_assert(!fence_strength_meets_scope(BS::ReleaseStore, MS::Gpu));
// Inner is not device-wide, so the rule is vacuous.
static_assert(fence_strength_meets_scope(BS::None, MS::Inner));

}  // namespace detail::fence_lowering_self_test

}  // namespace crucible::mimic
