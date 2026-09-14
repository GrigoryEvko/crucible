#pragma once

#include <crucible/Platform.h>
#include <crucible/algebra/lattices/BarrierStrengthLattice.h>
#include <crucible/algebra/lattices/MemoryScopeLattice.h>

#include <cstdint>
#include <utility>

namespace crucible::mimic {

using ::crucible::algebra::lattices::BarrierStrength;
using ::crucible::algebra::lattices::MemoryScope;
using ::crucible::algebra::lattices::MemoryScopeLattice;
using ::crucible::algebra::lattices::mem_scope_is_accel;
using ::crucible::algebra::lattices::mem_scope_is_arm;

// This enum is deliberately separate from the host-architecture tag used
// elsewhere. That tag enumerates host ISAs only, and fence lowering needs a
// device dialect and a pure compiler barrier as well.
enum class FenceArch : std::uint8_t {
    X86 = 0,
    Arm = 1,
    Gpu = 2,
    Compiler = 3,
};

enum class FenceKind : std::uint8_t {
    NoOp = 0,
    CompilerBarrier = 1,
    X86Mfence = 2,
    ArmDmb = 3,
    GpuFence = 4,
};

enum class FenceDomain : std::uint8_t {
    None = 0,
    ArmIsh = 1,
    ArmOsh = 2,
    ArmSy = 3,
    GpuCta = 4,
    GpuCluster = 5,
    GpuGpu = 6,
    GpuSys = 7,
};

enum class FenceOrder : std::uint8_t {
    None = 0,
    Acquire = 1,
    Release = 2,
    AcqRel = 3,
    SeqCst = 4,
    Full = 5,
};

struct FenceSpec {
    FenceKind kind = FenceKind::NoOp;
    FenceDomain domain = FenceDomain::None;
    FenceOrder order = FenceOrder::None;

    [[nodiscard]] constexpr bool operator==(const FenceSpec&) const noexcept = default;
};
static_assert(sizeof(FenceSpec) == 3, "FenceSpec is three uint8_t fields, no padding");
static_assert(alignof(FenceSpec) == 1);

[[nodiscard]] constexpr bool fence_arch_scope_consistent(FenceArch arch, MemoryScope scope) noexcept {
    if (mem_scope_is_accel(scope)) {
        return arch == FenceArch::Gpu;
    }
    if (mem_scope_is_arm(scope)) {
        return arch == FenceArch::Arm;
    }
    // Thread and System belong to no trunk. Any arch may fence at those
    // scopes.
    return true;
}

// A device-wide or system-wide scope demands at least AcqRel. lower_fence
// applies this only to the GPU dialect. On x86 and aarch64 System is the
// default coherence domain rather than a widened one, so an acquire fence at
// System scope there is legitimate. The race this rule prevents exists only
// on the accel trunk, where device visibility is a separate axis from the
// ordering the barrier establishes.
[[nodiscard]] constexpr bool fence_strength_meets_scope(BarrierStrength strength, MemoryScope scope) noexcept {
    if (MemoryScopeLattice::leq(MemoryScope::Gpu, scope)) {
        return std::to_underlying(strength) >= std::to_underlying(BarrierStrength::AcqRel);
    }
    return true;
}

namespace detail {

[[nodiscard]] constexpr FenceOrder order_of(BarrierStrength strength) noexcept {
    switch (strength) {
        case BarrierStrength::AcquireLoad:
            return FenceOrder::Acquire;
        case BarrierStrength::ReleaseStore:
            return FenceOrder::Release;
        case BarrierStrength::AcqRel:
            return FenceOrder::AcqRel;
        case BarrierStrength::SeqCst:
            return FenceOrder::SeqCst;
        case BarrierStrength::FullFence:
            return FenceOrder::Full;
        case BarrierStrength::None:
        case BarrierStrength::CompilerBarrier:
        default:
            return FenceOrder::None;
    }
}

[[nodiscard]] constexpr FenceDomain arm_domain_of(MemoryScope scope) noexcept {
    switch (scope) {
        case MemoryScope::Inner:
            return FenceDomain::ArmIsh;
        case MemoryScope::Outer:
            return FenceDomain::ArmOsh;
        default:
            return FenceDomain::ArmSy;
    }
}

[[nodiscard]] constexpr FenceDomain gpu_domain_of(MemoryScope scope) noexcept {
    switch (scope) {
        case MemoryScope::Cta:
            return FenceDomain::GpuCta;
        case MemoryScope::Cluster:
            return FenceDomain::GpuCluster;
        case MemoryScope::Gpu:
            return FenceDomain::GpuGpu;
        default:
            return FenceDomain::GpuSys;
    }
}

// PTX has no one-sided fence. Acquire-only and release-only fold to acq_rel,
// seqcst and full fold to sc.
[[nodiscard]] constexpr FenceOrder gpu_sem_of(BarrierStrength strength) noexcept {
    return (std::to_underlying(strength) >= std::to_underlying(BarrierStrength::SeqCst)) ? FenceOrder::SeqCst
                                                                                         : FenceOrder::AcqRel;
}

}  // namespace detail

[[nodiscard]] constexpr FenceSpec fence_spec_for(BarrierStrength strength, MemoryScope scope, FenceArch arch) noexcept {
    if (strength == BarrierStrength::None) {
        return FenceSpec{FenceKind::NoOp, FenceDomain::None, FenceOrder::None};
    }
    if (strength == BarrierStrength::CompilerBarrier) {
        return FenceSpec{FenceKind::CompilerBarrier, FenceDomain::None, FenceOrder::None};
    }

    // An inconsistent triple degrades to a compiler barrier. That is weaker
    // than the caller asked for, but the alternative is emitting an
    // instruction the target dialect has no encoding for.
    if (!fence_arch_scope_consistent(arch, scope)) {
        return FenceSpec{FenceKind::CompilerBarrier, FenceDomain::None, detail::order_of(strength)};
    }

    switch (arch) {
        case FenceArch::Compiler:
            return FenceSpec{FenceKind::CompilerBarrier, FenceDomain::None, detail::order_of(strength)};

        case FenceArch::X86:
            // x86 is TSO. Acquire, release and acq_rel need no hardware
            // fence, only an optimizer barrier.
            if (std::to_underlying(strength) >= std::to_underlying(BarrierStrength::SeqCst)) {
                return FenceSpec{FenceKind::X86Mfence, FenceDomain::None, FenceOrder::Full};
            }
            return FenceSpec{FenceKind::CompilerBarrier, FenceDomain::None, detail::order_of(strength)};

        case FenceArch::Arm:
            if (scope == MemoryScope::Thread) {
                return FenceSpec{FenceKind::CompilerBarrier, FenceDomain::None, detail::order_of(strength)};
            }
            return FenceSpec{FenceKind::ArmDmb, detail::arm_domain_of(scope), detail::order_of(strength)};

        case FenceArch::Gpu:
            // PTX has no warp scope token. Lanes in a warp advance in lock
            // step, so warp scope needs only an optimizer barrier.
            if (scope == MemoryScope::Thread || scope == MemoryScope::Warp) {
                return FenceSpec{FenceKind::CompilerBarrier, FenceDomain::None, detail::order_of(strength)};
            }
            return FenceSpec{FenceKind::GpuFence, detail::gpu_domain_of(scope), detail::gpu_sem_of(strength)};

        default:
            // Every FenceArch is handled above. This arm exists to satisfy
            // -Wswitch-default.
            return FenceSpec{FenceKind::CompilerBarrier, FenceDomain::None, FenceOrder::None};
    }
}

template <BarrierStrength Strength, MemoryScope Scope, FenceArch Arch>
[[nodiscard]] consteval FenceSpec lower_fence() noexcept {
    static_assert(fence_arch_scope_consistent(Arch, Scope),
                  "cross-trunk fence: an accel-trunk scope (Warp..Gpu) lowers only "
                  "on FenceArch::Gpu, an ARM-shareability scope (Inner/Outer) lowers "
                  "only on FenceArch::Arm.");
    static_assert(Arch != FenceArch::Gpu || fence_strength_meets_scope(Strength, Scope),
                  "device-or-wider GPU scope (Gpu / System) requires BarrierStrength "
                  ">= AcqRel. A weaker barrier widens visibility without establishing "
                  "two-sided ordering. x86 and aarch64 are exempt: System is their "
                  "default coherence domain, not a widened one, so acquire and release "
                  "fences there are valid.");
    return fence_spec_for(Strength, Scope, Arch);
}

[[nodiscard]] constexpr const char* fence_mnemonic(FenceSpec spec) noexcept {
    switch (spec.kind) {
        case FenceKind::NoOp:
            return "";
        case FenceKind::CompilerBarrier:
            return "compiler_barrier";
        case FenceKind::X86Mfence:
            return "mfence";
        case FenceKind::ArmDmb:
            switch (spec.domain) {
                case FenceDomain::ArmIsh:
                    return spec.order == FenceOrder::Acquire ? "dmb ishld"
                         : spec.order == FenceOrder::Release ? "dmb ishst"
                                                             : "dmb ish";
                case FenceDomain::ArmOsh:
                    return spec.order == FenceOrder::Acquire ? "dmb oshld"
                         : spec.order == FenceOrder::Release ? "dmb oshst"
                                                             : "dmb osh";
                case FenceDomain::ArmSy:
                    return spec.order == FenceOrder::Acquire ? "dmb ld"
                         : spec.order == FenceOrder::Release ? "dmb st"
                                                             : "dmb sy";
                default:
                    return "dmb sy";
            }
        case FenceKind::GpuFence:
            switch (spec.domain) {
                case FenceDomain::GpuCta:
                    return spec.order == FenceOrder::SeqCst ? "fence.sc.cta" : "fence.acq_rel.cta";
                case FenceDomain::GpuCluster:
                    return spec.order == FenceOrder::SeqCst ? "fence.sc.cluster" : "fence.acq_rel.cluster";
                case FenceDomain::GpuGpu:
                    return spec.order == FenceOrder::SeqCst ? "fence.sc.gpu" : "fence.acq_rel.gpu";
                case FenceDomain::GpuSys:
                    return spec.order == FenceOrder::SeqCst ? "fence.sc.sys" : "fence.acq_rel.sys";
                default:
                    return "fence.acq_rel.sys";
            }
        default:
            return "";
    }
}

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

// The static_asserts above exercise only the consteval path. This runs the
// same table with non-constant arguments so a fault in the runtime path is
// caught too.
inline void runtime_smoke_test() noexcept {
    volatile auto s = BarrierStrength::AcqRel;
    volatile auto sc = MemoryScope::Cta;
    volatile auto a = FenceArch::Gpu;
    FenceSpec spec =
        fence_spec_for(static_cast<BarrierStrength>(s), static_cast<MemoryScope>(sc), static_cast<FenceArch>(a));
    // The volatile reads launder the arguments past the constant folder, which
    // is the point of this function: the compiler cannot know what
    // fence_spec_for returns here, so these are claims about its runtime
    // output rather than facts.  Under [[assume]] they would state that output
    // as a premise and let it propagate into the fence_mnemonic switch below,
    // which turns the one fault this test exists to catch into a miscompile.
    // They check in every build mode instead.
    CRUCIBLE_FATAL_INVARIANT(spec.kind == FenceKind::GpuFence);
    CRUCIBLE_FATAL_INVARIANT(spec.domain == FenceDomain::GpuCta);
    const char* m = fence_mnemonic(spec);
    // Every return in fence_mnemonic is a string literal, so this one the
    // compiler can prove on its own. It is kept as a real check rather than a
    // hint because nothing downstream consumes m, which leaves a hint with no
    // optimization to inform and this function with one assertion fewer.
    CRUCIBLE_FATAL_INVARIANT(m != nullptr);
}

}  // namespace crucible::mimic
