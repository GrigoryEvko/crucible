// FIXY-V-272 sentinel TU — verifies mimic/Fence.h under the project's
// warning/contract flags (header-only static_asserts are only checked when
// a real .cpp TU includes the header — see feedback_header_only_static_
// assert_blind_spot).  Re-asserts the lowering table + the V401/V402 gate
// predicates, then exercises the constexpr table at runtime via
// runtime_smoke_test().

#include <crucible/mimic/Fence.h>

#include <cstdio>

namespace mf = crucible::mimic;
using BS = crucible::algebra::lattices::BarrierStrength;
using MS = crucible::algebra::lattices::MemoryScope;
using FA = mf::FenceArch;

// ── Lowering table (mirrors the in-header golden table) ──────────────
static_assert(mf::lower_fence<BS::None, MS::Thread, FA::X86>().kind == mf::FenceKind::NoOp);
static_assert(mf::lower_fence<BS::CompilerBarrier, MS::Thread, FA::Gpu>().kind ==
              mf::FenceKind::CompilerBarrier);

// x86: free acquire/release (TSO), mfence for seqcst/full.
static_assert(mf::lower_fence<BS::AcquireLoad, MS::System, FA::X86>().kind ==
              mf::FenceKind::CompilerBarrier);
static_assert(mf::lower_fence<BS::SeqCst, MS::System, FA::X86>().kind == mf::FenceKind::X86Mfence);

// ARM DMB domains + variants.
static_assert(mf::lower_fence<BS::AcqRel, MS::Inner, FA::Arm>() ==
              mf::FenceSpec{mf::FenceKind::ArmDmb, mf::FenceDomain::ArmIsh, mf::FenceOrder::AcqRel});
static_assert(mf::lower_fence<BS::ReleaseStore, MS::Outer, FA::Arm>().domain ==
              mf::FenceDomain::ArmOsh);
static_assert(mf::lower_fence<BS::FullFence, MS::System, FA::Arm>().domain == mf::FenceDomain::ArmSy);

// GPU/PTX scope tokens + .acq_rel / .sc.
static_assert(mf::lower_fence<BS::AcqRel, MS::Cta, FA::Gpu>().domain == mf::FenceDomain::GpuCta);
static_assert(mf::lower_fence<BS::SeqCst, MS::Gpu, FA::Gpu>().order == mf::FenceOrder::SeqCst);
static_assert(mf::lower_fence<BS::AcqRel, MS::Warp, FA::Gpu>().kind == mf::FenceKind::CompilerBarrier);

// Mnemonics — golden assembly tokens.
static_assert(mf::fence_mnemonic(mf::lower_fence<BS::SeqCst, MS::System, FA::X86>())[0] == 'm');
static_assert(mf::fence_mnemonic(mf::lower_fence<BS::SeqCst, MS::Gpu, FA::Gpu>())[0] == 'f');

// Gate predicates.
static_assert(mf::fence_arch_scope_consistent(FA::Gpu, MS::Cta));
static_assert(!mf::fence_arch_scope_consistent(FA::X86, MS::Cta));
static_assert(!mf::fence_strength_meets_scope(BS::ReleaseStore, MS::Gpu));

int main() {
    mf::runtime_smoke_test();

    // A couple of runtime table lookups to keep the constexpr path honest
    // outside a constant-evaluation context.
    volatile int sel = 0;
    auto strength = sel == 0 ? BS::SeqCst : BS::None;
    auto spec = mf::fence_spec_for(strength, MS::System, FA::X86);
    if (spec.kind != mf::FenceKind::X86Mfence) {
        std::fprintf(stderr, "V272 SENTINEL FAIL: x86 system seqcst should lower to mfence\n");
        return 1;
    }
    std::printf("V272 SENTINEL OK (%s)\n", mf::fence_mnemonic(spec));
    return 0;
}
