#pragma once

// ── crucible::mimic — fence lowering table (FIXY-V-272) ─────────────
//
// THE consteval lowering step from the abstract typed-fence axes
// (BarrierStrength × MemoryScope × FenceArch) down to a CONCRETE fence
// SPEC that per-vendor Mimic backends consume to emit the actual machine
// instruction.  The three input axes are:
//
//   * BarrierStrength (V-252, algebra/lattices/BarrierStrengthLattice.h)
//       None ⊑ CompilerBarrier ⊑ AcquireLoad ⊑ ReleaseStore ⊑ AcqRel
//       ⊑ SeqCst ⊑ FullFence — how much ordering the region claims.
//   * MemoryScope (V-265, algebra/lattices/MemoryScopeLattice.h)
//       Thread ⊥ · accel trunk (Warp ⊑ Cta ⊑ Cluster ⊑ Gpu) · ARM trunk
//       (Inner ⊑ Outer) · System ⊤ — how wide the visibility is.
//   * FenceArch (below) — the fence DIALECT the target speaks.
//
// ── Why a mimic-local FenceArch (and not source::ArchTag) ───────────
//
// `source::ArchTag` (safety/source/Arch.h) is the CPU-HOST trunk only —
// {X86, Arm, Portable}.  It has NO GPU enumerator because an ArchPinned
// tag pins a value to a host ISA, not a device ISA.  Fence lowering needs
// FOUR dialects — x86 `mfence`, aarch64 `DMB`, PTX/GPU `fence.<sem>.<scope>`,
// and a pure compiler barrier — so this header ships its own taxonomy.
// FenceArch is 1:1 with the fixy `BarrierArch` (V-269 fixy/Hw.h:
// {X86, Arm, Compiler, Gpu}) by deliberate correspondence: a Mimic
// backend lowering a fixy `grant::hw::scope<Scope, BarrierArch>` maps the
// grant's BarrierArch onto this FenceArch and calls lower_fence.  mimic/
// stays independent of fixy/ (mimic is the codegen layer; fixy is the
// typed surface that gets lowered THROUGH it), so the enums are parallel
// rather than shared.
//
// ── Two consistency gates (mirrors of CollisionCatalog V401/V402) ───
//
// `lower_fence<Strength, Scope, Arch>()` is consteval and static_asserts
// BOTH cross-axis rules the typed surface (ScopedFence × BarrierGuarded)
// already enforces, so an inconsistent triple can NEVER produce a (wrong)
// spec — it is a hard compile error at the lowering boundary:
//
//   * V402 (trunk consistency): an accel-trunk scope (Warp..Gpu) lowers
//     ONLY on FenceArch::Gpu (the mfence / DMB dialects cannot realize a
//     PTX `.cta`/`.gpu` scope); an ARM-shareability scope (Inner/Outer)
//     lowers ONLY on FenceArch::Arm (x86 has no ISH/OSH domain).
//     Thread (⊥) and System (⊤) are shared sentinels — any arch.
//   * V401 (device-or-wider needs AcqRel): a fence at scope ⊒ Gpu
//     (Gpu / System) MUST be at least AcqRel — a device-visible publish
//     guarded only by None / CompilerBarrier / acquire / release widens
//     visibility but never establishes the two-sided ordering device
//     readers require (a silent weak-memory race).
//
// `fence_spec_for(strength, scope, arch)` is the constexpr core the
// consteval form delegates to; it is total (no UB on any input) and
// usable at runtime for table-driven lowering, but does NOT itself
// static_assert — it documents the consistency precondition and the
// consteval `lower_fence` is the validated entry point (CLAUDE.md
// boundary-validates / inner-trusts pattern).
//
// ── Axiom coverage ──────────────────────────────────────────────────
//
//   TypeSafe — FenceArch / FenceKind / FenceDomain / FenceOrder are all
//                strong scoped enums (`enum class : uint8_t`); the input
//                axes (BarrierStrength, MemoryScope) are likewise strong.
//   InitSafe — every FenceSpec field has an NSDMI; every enum has explicit
//                ordinals.  The golden static_assert table below fires if
//                any (Strength, Scope, Arch) cell regresses.
//   DetSafe  — fence_spec_for is `constexpr` (not consteval) so a runtime
//                table-driven caller gets the SAME spec a compile-time
//                caller does; lower_fence is `consteval` (compile-time
//                table).  No platform-dependent branching.
//   NullSafe — fence_mnemonic returns a pointer into a static string
//                literal (never null); [[nodiscard]] on every query.
//   MemSafe / LeakSafe — zero state, zero allocation; FenceSpec is a
//                3-byte POD.  sizeof asserted below.
//
// ── Runtime cost ────────────────────────────────────────────────────
//
// Zero.  lower_fence is consteval; fence_spec_for is constexpr and folds
// to a single FenceSpec immediate at every compile-time use site.

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

// ── FenceArch — the fence dialect a target speaks ───────────────────
// 1:1 with fixy BarrierArch (X86, Arm, Compiler, Gpu); see header doc.
enum class FenceArch : std::uint8_t {
    X86      = 0,  // x86-64: mfence (full); acquire/release are free (TSO)
    Arm      = 1,  // aarch64: DMB <domain><variant>
    Gpu      = 2,  // PTX/GPU: fence.<sem>.<scope> (vendor backend dialect)
    Compiler = 3,  // pure compiler reordering barrier; no hardware fence
};

// ── FenceKind — the instruction FAMILY a backend emits ──────────────
enum class FenceKind : std::uint8_t {
    NoOp            = 0,  // None strength: nothing emitted
    CompilerBarrier = 1,  // asm volatile("" ::: "memory"): optimizer-only
    X86Mfence       = 2,  // x86 mfence (standalone full fence)
    ArmDmb          = 3,  // aarch64 DMB <domain><variant>
    GpuFence        = 4,  // PTX fence.<sem>.<scope> (vendor backend dialect)
};

// ── FenceDomain — the scope token carried by the instruction ────────
enum class FenceDomain : std::uint8_t {
    None       = 0,  // x86 system / compiler barrier / noop — no scope token
    ArmIsh     = 1,  // DMB ISH  (inner-shareable)
    ArmOsh     = 2,  // DMB OSH  (outer-shareable)
    ArmSy      = 3,  // DMB SY   (full-system)
    GpuCta     = 4,  // fence....cta
    GpuCluster = 5,  // fence....cluster
    GpuGpu     = 6,  // fence....gpu
    GpuSys     = 7,  // fence....sys
};

// ── FenceOrder — the ordering variant ───────────────────────────────
enum class FenceOrder : std::uint8_t {
    None    = 0,
    Acquire = 1,  // ARM DMB <dom>LD / PTX fence.acq_rel (two-sided)
    Release = 2,  // ARM DMB <dom>ST / PTX fence.acq_rel (two-sided)
    AcqRel  = 3,  // ARM DMB <dom> (full) / PTX fence.acq_rel
    SeqCst  = 4,  // PTX fence.sc / x86 mfence
    Full    = 5,  // standalone architectural full fence
};

// ── FenceSpec — the descriptor backends consume ─────────────────────
struct FenceSpec {
    FenceKind   kind   = FenceKind::NoOp;
    FenceDomain domain = FenceDomain::None;
    FenceOrder  order  = FenceOrder::None;

    [[nodiscard]] constexpr bool operator==(const FenceSpec&) const noexcept = default;
};
static_assert(sizeof(FenceSpec) == 3, "FenceSpec is three uint8_t fields, no padding");
static_assert(alignof(FenceSpec) == 1);

// ── Consistency predicates (V402 / V401) ────────────────────────────

// V402 mirror: which (arch, scope) trunks may compose.
[[nodiscard]] constexpr bool fence_arch_scope_consistent(FenceArch arch,
                                                         MemoryScope scope) noexcept {
    if (mem_scope_is_accel(scope)) {
        return arch == FenceArch::Gpu;
    }
    if (mem_scope_is_arm(scope)) {
        return arch == FenceArch::Arm;
    }
    // Thread (⊥) / System (⊤): shared sentinels, any arch.
    return true;
}

// V401 mirror (pure scope × strength rule): scope ⊒ Gpu (device-or-wider)
// demands strength ⊒ AcqRel.  lower_fence applies this ONLY to the GPU
// dialect (Arch == Gpu) — x86 / aarch64 have no independent scope axis
// (System is their DEFAULT coherence domain, not a widened one), so an
// x86 acquire load at System scope is a legitimate, free-on-TSO operation
// that V401 must not reject; the scope-widening race the catalog warns
// about is real only on the accel trunk, where `.gpu` visibility is a
// separate axis from the ordering the barrier establishes.
[[nodiscard]] constexpr bool fence_strength_meets_scope(BarrierStrength strength,
                                                       MemoryScope scope) noexcept {
    if (MemoryScopeLattice::leq(MemoryScope::Gpu, scope)) {
        // scope is Gpu-wide or System-wide; require at least AcqRel.
        return std::to_underlying(strength) >= std::to_underlying(BarrierStrength::AcqRel);
    }
    return true;
}

namespace detail {

// Maps a per-strength ordering variant onto the FenceOrder carried by an
// emitted hardware fence (used by the Arm / Gpu / x86-compiler-barrier
// arms; None / CompilerBarrier strengths are handled before this).
[[nodiscard]] constexpr FenceOrder order_of(BarrierStrength strength) noexcept {
    switch (strength) {
        case BarrierStrength::AcquireLoad:  return FenceOrder::Acquire;
        case BarrierStrength::ReleaseStore: return FenceOrder::Release;
        case BarrierStrength::AcqRel:       return FenceOrder::AcqRel;
        case BarrierStrength::SeqCst:       return FenceOrder::SeqCst;
        case BarrierStrength::FullFence:    return FenceOrder::Full;
        case BarrierStrength::None:
        case BarrierStrength::CompilerBarrier:
        default:                            return FenceOrder::None;
    }
}

[[nodiscard]] constexpr FenceDomain arm_domain_of(MemoryScope scope) noexcept {
    switch (scope) {
        case MemoryScope::Inner: return FenceDomain::ArmIsh;
        case MemoryScope::Outer: return FenceDomain::ArmOsh;
        default:                 return FenceDomain::ArmSy;  // System (full-system)
    }
}

[[nodiscard]] constexpr FenceDomain gpu_domain_of(MemoryScope scope) noexcept {
    switch (scope) {
        case MemoryScope::Cta:     return FenceDomain::GpuCta;
        case MemoryScope::Cluster: return FenceDomain::GpuCluster;
        case MemoryScope::Gpu:     return FenceDomain::GpuGpu;
        default:                   return FenceDomain::GpuSys;  // System (.sys)
    }
}

// PTX fences are two-sided; acquire-only / release-only fold to acq_rel,
// seqcst / full fold to .sc.
[[nodiscard]] constexpr FenceOrder gpu_sem_of(BarrierStrength strength) noexcept {
    return (std::to_underlying(strength) >= std::to_underlying(BarrierStrength::SeqCst))
               ? FenceOrder::SeqCst
               : FenceOrder::AcqRel;
}

}  // namespace detail

// ── fence_spec_for — the constexpr lowering table ───────────────────
//
// Total over all inputs (no UB).  Correctness for codegen is guaranteed
// only for (arch, scope) pairs that satisfy fence_arch_scope_consistent
// and (strength, scope) pairs that satisfy fence_strength_meets_scope —
// the consteval lower_fence below static_asserts both.  For an
// inconsistent triple reached at runtime, the table degrades to a
// CompilerBarrier (a safe over-approximation: never WRONG codegen, only
// possibly weaker than the caller's unsatisfiable request) rather than
// fabricating a nonexistent instruction.
[[nodiscard]] constexpr FenceSpec
fence_spec_for(BarrierStrength strength, MemoryScope scope, FenceArch arch) noexcept {
    if (strength == BarrierStrength::None) {
        return FenceSpec{FenceKind::NoOp, FenceDomain::None, FenceOrder::None};
    }
    if (strength == BarrierStrength::CompilerBarrier) {
        return FenceSpec{FenceKind::CompilerBarrier, FenceDomain::None, FenceOrder::None};
    }

    // Cross-trunk request that should never reach here on a validated
    // triple: safe-degrade to a compiler barrier rather than emit a
    // wrong-dialect instruction.
    if (!fence_arch_scope_consistent(arch, scope)) {
        return FenceSpec{FenceKind::CompilerBarrier, FenceDomain::None, detail::order_of(strength)};
    }

    switch (arch) {
        case FenceArch::Compiler:
            // A compiler-arch fence never emits a hardware instruction.
            return FenceSpec{FenceKind::CompilerBarrier, FenceDomain::None, detail::order_of(strength)};

        case FenceArch::X86:
            // x86 TSO: acquire / release / acq_rel are free — only an
            // optimizer barrier is needed.  SeqCst / FullFence emit mfence.
            if (std::to_underlying(strength) >= std::to_underlying(BarrierStrength::SeqCst)) {
                return FenceSpec{FenceKind::X86Mfence, FenceDomain::None, FenceOrder::Full};
            }
            return FenceSpec{FenceKind::CompilerBarrier, FenceDomain::None, detail::order_of(strength)};

        case FenceArch::Arm:
            // Thread (⊥): thread-local visibility, no DMB — optimizer barrier.
            if (scope == MemoryScope::Thread) {
                return FenceSpec{FenceKind::CompilerBarrier, FenceDomain::None, detail::order_of(strength)};
            }
            return FenceSpec{FenceKind::ArmDmb, detail::arm_domain_of(scope), detail::order_of(strength)};

        case FenceArch::Gpu:
            // Thread (⊥) / Warp (lock-step convergent, no `.warp` token):
            // no device fence — optimizer barrier.
            if (scope == MemoryScope::Thread || scope == MemoryScope::Warp) {
                return FenceSpec{FenceKind::CompilerBarrier, FenceDomain::None, detail::order_of(strength)};
            }
            return FenceSpec{FenceKind::GpuFence, detail::gpu_domain_of(scope), detail::gpu_sem_of(strength)};

        default:
            // FenceArch is exhaustively handled above; this arm satisfies
            // -Wswitch-default and degrades to a compiler barrier.
            return FenceSpec{FenceKind::CompilerBarrier, FenceDomain::None, FenceOrder::None};
    }
}

// ── lower_fence — THE consteval lowering entry point ────────────────
//
// Validates the triple at compile time (V402 trunk consistency + V401
// device-or-wider AcqRel) then returns fence_spec_for.  An inconsistent
// triple is a hard compile error here — the lowering table refuses to
// lower an unsound fence.
template <BarrierStrength Strength, MemoryScope Scope, FenceArch Arch>
[[nodiscard]] consteval FenceSpec lower_fence() noexcept {
    static_assert(fence_arch_scope_consistent(Arch, Scope),
                  "FIXY-V-272 lower_fence: cross-trunk fence — an accel-trunk scope "
                  "(Warp..Gpu) lowers only on FenceArch::Gpu, an ARM-shareability "
                  "scope (Inner/Outer) lowers only on FenceArch::Arm (V402 mirror).");
    static_assert(Arch != FenceArch::Gpu || fence_strength_meets_scope(Strength, Scope),
                  "FIXY-V-272 lower_fence: device-or-wider GPU scope (Gpu / System) "
                  "requires BarrierStrength >= AcqRel — a weaker barrier widens "
                  "visibility without establishing two-sided ordering (V401 mirror). "
                  "x86 / aarch64 are exempt: System is their default coherence domain, "
                  "not a widened one, so acquire / release fences there are valid.");
    return fence_spec_for(Strength, Scope, Arch);
}

// ── fence_mnemonic — human-readable + golden-CI assembly token ──────
// Derived purely from the spec (single source of truth); returns a
// pointer into a static string literal — never null, never allocates.
[[nodiscard]] constexpr const char* fence_mnemonic(FenceSpec spec) noexcept {
    switch (spec.kind) {
        case FenceKind::NoOp:            return "";
        case FenceKind::CompilerBarrier: return "compiler_barrier";
        case FenceKind::X86Mfence:       return "mfence";
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
                default: return "dmb sy";
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
                default: return "fence.acq_rel.sys";
            }
        default:
            return "";
    }
}

// ── Golden lowering table — compile-time regression witness ─────────
namespace detail::fence_lowering_self_test {

using BS = BarrierStrength;
using MS = MemoryScope;
using FA = FenceArch;

// None / CompilerBarrier degenerate regardless of arch/scope.
static_assert(lower_fence<BS::None, MS::System, FA::X86>().kind == FenceKind::NoOp);
static_assert(lower_fence<BS::CompilerBarrier, MS::System, FA::Arm>().kind == FenceKind::CompilerBarrier);

// x86: acquire/release/acqrel are free (compiler barrier on TSO); seqcst/full → mfence.
static_assert(lower_fence<BS::AcqRel, MS::System, FA::X86>().kind == FenceKind::CompilerBarrier);
static_assert(lower_fence<BS::SeqCst, MS::System, FA::X86>().kind == FenceKind::X86Mfence);
static_assert(lower_fence<BS::FullFence, MS::System, FA::X86>().kind == FenceKind::X86Mfence);

// ARM: scope → DMB domain, strength → variant.
static_assert(lower_fence<BS::AcqRel, MS::Inner, FA::Arm>() ==
              FenceSpec{FenceKind::ArmDmb, FenceDomain::ArmIsh, FenceOrder::AcqRel});
static_assert(lower_fence<BS::AcquireLoad, MS::Inner, FA::Arm>().order == FenceOrder::Acquire);
static_assert(lower_fence<BS::ReleaseStore, MS::Outer, FA::Arm>() ==
              FenceSpec{FenceKind::ArmDmb, FenceDomain::ArmOsh, FenceOrder::Release});
static_assert(lower_fence<BS::SeqCst, MS::System, FA::Arm>().domain == FenceDomain::ArmSy);
static_assert(lower_fence<BS::AcqRel, MS::Thread, FA::Arm>().kind == FenceKind::CompilerBarrier);

// GPU/PTX: scope → token, strength → .acq_rel / .sc.
static_assert(lower_fence<BS::AcqRel, MS::Cta, FA::Gpu>() ==
              FenceSpec{FenceKind::GpuFence, FenceDomain::GpuCta, FenceOrder::AcqRel});
static_assert(lower_fence<BS::SeqCst, MS::Gpu, FA::Gpu>() ==
              FenceSpec{FenceKind::GpuFence, FenceDomain::GpuGpu, FenceOrder::SeqCst});
static_assert(lower_fence<BS::AcqRel, MS::Cluster, FA::Gpu>().domain == FenceDomain::GpuCluster);
static_assert(lower_fence<BS::AcqRel, MS::Warp, FA::Gpu>().kind == FenceKind::CompilerBarrier);

// Mnemonics — the golden assembly tokens backends / CI compare against.
static_assert(fence_mnemonic(lower_fence<BS::SeqCst, MS::System, FA::X86>())[0] == 'm');  // "mfence"
static_assert(fence_mnemonic(lower_fence<BS::AcqRel, MS::Inner, FA::Arm>())[4] == 'i');   // "dmb ish"
static_assert(fence_mnemonic(lower_fence<BS::SeqCst, MS::Gpu, FA::Gpu>())[6] == 's');     // "fence.sc.gpu"
static_assert(fence_mnemonic(FenceSpec{})[0] == '\0');                                    // NoOp → ""

// Consistency predicates.
static_assert(fence_arch_scope_consistent(FA::Gpu, MS::Cta));
static_assert(!fence_arch_scope_consistent(FA::X86, MS::Cta));
static_assert(!fence_arch_scope_consistent(FA::Arm, MS::Gpu));
static_assert(!fence_arch_scope_consistent(FA::Gpu, MS::Inner));
static_assert(fence_arch_scope_consistent(FA::X86, MS::System));
static_assert(fence_strength_meets_scope(BS::AcqRel, MS::Gpu));
static_assert(!fence_strength_meets_scope(BS::ReleaseStore, MS::Gpu));
static_assert(fence_strength_meets_scope(BS::None, MS::Inner));  // not Gpu-wide → vacuous

}  // namespace detail::fence_lowering_self_test

// ── Runtime smoke test (per algebra/effects discipline) ─────────────
// Exercises the constexpr table at RUNTIME with non-constant args so the
// non-consteval path is verified (consteval-only static_asserts can mask
// inline-body bugs).  Called from the V-272 sentinel TU.
inline void runtime_smoke_test() noexcept {
    volatile auto s = BarrierStrength::AcqRel;
    volatile auto sc = MemoryScope::Cta;
    volatile auto a = FenceArch::Gpu;
    FenceSpec spec = fence_spec_for(static_cast<BarrierStrength>(s),
                                    static_cast<MemoryScope>(sc),
                                    static_cast<FenceArch>(a));
    CRUCIBLE_INVARIANT(spec.kind == FenceKind::GpuFence);
    CRUCIBLE_INVARIANT(spec.domain == FenceDomain::GpuCta);
    const char* m = fence_mnemonic(spec);
    CRUCIBLE_INVARIANT(m != nullptr);
}

}  // namespace crucible::mimic
