// Sentinel TU: compiles the header under the project warning and contract flags
// so its static_asserts run.

#include <crucible/mimic/Fence.h>

#include <cstdio>

namespace mf = crucible::mimic;
using BS = crucible::algebra::lattices::BarrierStrength;
using MS = crucible::algebra::lattices::MemoryScope;
using FA = mf::FenceArch;

// These assertions duplicate the golden lowering table on purpose.  A change to
// the table has to be made in both places.
static_assert(mf::lower_fence<BS::None, MS::Thread, FA::X86>().kind == mf::FenceKind::NoOp);
static_assert(mf::lower_fence<BS::CompilerBarrier, MS::Thread, FA::Gpu>().kind == mf::FenceKind::CompilerBarrier);

// x86 is TSO, so acquire and release are free.  Only seqcst and full need
// mfence.
static_assert(mf::lower_fence<BS::AcquireLoad, MS::System, FA::X86>().kind == mf::FenceKind::CompilerBarrier);
static_assert(mf::lower_fence<BS::SeqCst, MS::System, FA::X86>().kind == mf::FenceKind::X86Mfence);

static_assert(mf::lower_fence<BS::AcqRel, MS::Inner, FA::Arm>()
              == mf::FenceSpec{mf::FenceKind::ArmDmb, mf::FenceDomain::ArmIsh, mf::FenceOrder::AcqRel});
static_assert(mf::lower_fence<BS::ReleaseStore, MS::Outer, FA::Arm>().domain == mf::FenceDomain::ArmOsh);
static_assert(mf::lower_fence<BS::FullFence, MS::System, FA::Arm>().domain == mf::FenceDomain::ArmSy);

static_assert(mf::lower_fence<BS::AcqRel, MS::Cta, FA::Gpu>().domain == mf::FenceDomain::GpuCta);
static_assert(mf::lower_fence<BS::SeqCst, MS::Gpu, FA::Gpu>().order == mf::FenceOrder::SeqCst);
static_assert(mf::lower_fence<BS::AcqRel, MS::Warp, FA::Gpu>().kind == mf::FenceKind::CompilerBarrier);

// The mnemonic is checked by first character only: m for mfence, f for the PTX
// fence.
static_assert(mf::fence_mnemonic(mf::lower_fence<BS::SeqCst, MS::System, FA::X86>())[0] == 'm');
static_assert(mf::fence_mnemonic(mf::lower_fence<BS::SeqCst, MS::Gpu, FA::Gpu>())[0] == 'f');

static_assert(mf::fence_arch_scope_consistent(FA::Gpu, MS::Cta));
static_assert(!mf::fence_arch_scope_consistent(FA::X86, MS::Cta));
static_assert(!mf::fence_strength_meets_scope(BS::ReleaseStore, MS::Gpu));

int main() {
    mf::runtime_smoke_test();

    // The volatile read forces the lookup out of a constant-evaluation context.
    volatile int sel = 0;
    auto strength = sel == 0 ? BS::SeqCst : BS::None;
    auto spec = mf::fence_spec_for(strength, MS::System, FA::X86);
    if (spec.kind != mf::FenceKind::X86Mfence) {
        std::fprintf(stderr, "fence: x86 system seqcst must lower to mfence\n");
        return 1;
    }
    std::printf("fence lowering OK (%s)\n", mf::fence_mnemonic(spec));
    return 0;
}
