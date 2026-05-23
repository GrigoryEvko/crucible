#pragma once

// ── crucible::safety — DimensionTraits.h (Phase 0 P0-3) ─────────────
//
// The Tier S / L / T / F / V dispatch vocabulary that `safety/Fn.h`
// (Phase 0 P0-1) and `safety/CollisionCatalog.h` (Phase 0 P0-2)
// dispatch on per fixy.md §24.1.  This header ships:
//
//   1. TierKind enum — the 5 composition-law families.
//   2. DimensionAxis enum — the 20 dimensions per fixy.md §24.1
//      (FX's 22 minus dim 12 Clock Domain and dim 17 FP Order; both
//      drops justified in fixy.md §24.1 + §24.14).
//   3. tier_of_axis() — fixy.md §24.1 hard-coded mapping.
//   4. SemiringGrade / LatticeGrade / TypestateGrade /
//      FoundationalGrade / VersionedGrade — the 5 concept families
//      asserting the structural shape a grade type must carry to
//      participate in its Tier's composition law.
//   5. tier_for_grade<G> — best-effort grade-to-Tier classification
//      based on which concept G satisfies.  Specializable.
//
// Per fixy.md §24.1 the Tier table is the authoritative source of
// truth: each dimension is classified at exactly ONE Tier.  Tier
// determines the COMPOSITION LAW used at par/seq sites:
//
//   Tier S — Commutative semiring (par=+, seq=*, 0 annihilator)
//   Tier L — Lattice with validity check (par=join, seq=meet, valid_D)
//   Tier T — Typestate (transitions; no par/seq composition)
//   Tier F — Foundational (bidirectional elaboration + concept gates)
//   Tier V — Versioned (consistency check at each site)
//
// The 5 concept families are STRUCTURAL — they assert that a grade
// type carries the operations needed for its Tier's composition law.
// Concepts overlap by design: QttSemiring satisfies BOTH SemiringGrade
// AND LatticeGrade because the underlying carrier supports both
// composition laws.  Tier classification picks WHICH composition is
// used at par/seq sites for a given dimension.
//
// ── Why not detect Tier from grade structure alone? ─────────────────
//
// The grade carrier may satisfy multiple Tier concepts (every Semiring
// is also a Lattice for our purposes).  The per-dimension Tier comes
// from the dimension's declaration in fixy.md §24.1, NOT from the
// grade's concept satisfaction.  `tier_for_grade` is a best-effort
// heuristic for cases where the dimension is unknown; the canonical
// path is `tier_of_axis(D)` for D : DimensionAxis.
//
// ── Axiom coverage ──────────────────────────────────────────────────
//
//   TypeSafe   — concept rejections produce structured static_assert
//                output at template-substitution time.
//   InitSafe   — every enum has a name function + reflection-driven
//                coverage assertion + sentinel-leak check.
//   DetSafe    — operations are constexpr / consteval; no runtime
//                nondeterminism path.
//   LeakSafe   — zero-state types; no resources.
//
// ── Runtime cost ────────────────────────────────────────────────────
//
// Zero on the hot path.  Concept gates fire at template instantiation;
// `tier_kind_name` / `dimension_axis_name` / `tier_of_axis` are
// constexpr (callable from runtime smoke tests AND from the consteval
// reflection-driven coverage helpers — per the algebra/Lattice.h
// convention "MUST be constexpr, NOT consteval"); the variable-template
// surface compiles to immediate values under -O3.
//
// ── Extension policy ────────────────────────────────────────────────
//
// Adding a new dimension is a four-step structural change:
//
//   1. Append a new enumerator to `DimensionAxis` — APPEND-ONLY.
//      Inserting into the middle would change indices of subsequent
//      enumerators and break per-wrapper trait specializations that
//      cite the dim by value.
//   2. Add the arm to `dimension_axis_name`'s switch.
//   3. Add the arm to `tier_of_axis`'s switch.
//   4. Reflection-driven self-tests re-fire automatically; if the
//      enumerator is added without the matching arms, build fails
//      with a named assertion identifying which switch is incomplete.
//
// Per fixy.md §24.14 wall-clock dimensions (Energy / Latency / Power /
// WallClock / BitsTransferred) are PROHIBITED — the compiler cannot
// prove physical bounds; annotation-only dimensions create false
// guarantees.  CI guard: PR adding such a dimension blocked by the
// extension policy enforcer.
//
// ── References ─────────────────────────────────────────────────────
//
//   misc/fixy.md §24.1            — the 20-dimension grade vector
//   misc/fixy.md §24.14           — FX inheritance map (drop list)
//   misc/02_05_2026.md            — Phase 0 commitment (P0-3 row)
//   crucible/algebra/Lattice.h    — Lattice / Semiring concepts
//   crucible/algebra/GradedTrait.h — GradedWrapper concept

#include <crucible/algebra/GradedTrait.h>
#include <crucible/algebra/Lattice.h>
#include <crucible/safety/AllocClass.h>
#include <crucible/safety/BarrierGuarded.h>
#include <crucible/safety/Budgeted.h>
#include <crucible/safety/CipherTier.h>
#include <crucible/safety/Consistency.h>
#include <crucible/safety/Crash.h>
#include <crucible/safety/DetSafe.h>
#include <crucible/safety/EpochVersioned.h>
#include <crucible/safety/HotPath.h>
#include <crucible/safety/Hw.h>
#include <crucible/safety/Linear.h>
#include <crucible/safety/MemOrder.h>
#include <crucible/safety/Mutation.h>
#include <crucible/safety/NumaPlacement.h>
#include <crucible/safety/NumericalTier.h>
#include <crucible/safety/OpaqueLifetime.h>
#include <crucible/safety/Progress.h>
#include <crucible/safety/RecipeSpec.h>
#include <crucible/safety/Refined.h>
#include <crucible/safety/ResidencyHeat.h>
#include <crucible/safety/SealedRefined.h>
#include <crucible/safety/Secret.h>
#include <crucible/safety/Stale.h>
#include <crucible/safety/Tagged.h>
#include <crucible/safety/TimeOrdered.h>
#include <crucible/safety/Vendor.h>
#include <crucible/safety/FpMode.h>
#include <crucible/safety/JoinPolicy.h>
#include <crucible/safety/Wait.h>
#include <crucible/safety/Witness.h>

#include <concepts>
#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>

namespace crucible::safety {

// ═════════════════════════════════════════════════════════════════════
// ── TierKind — the 5 composition-law families ──────────────────────
// ═════════════════════════════════════════════════════════════════════

enum class TierKind : std::uint8_t {
    Semiring     = 0,  // Tier S — par=+, seq=*, 0 annihilator (26 dims)
    Lattice      = 1,  // Tier L — par=join, seq=meet, valid_D check (2 dims)
    Typestate    = 2,  // Tier T — transitions on state; no par/seq (1 dim)
    Foundational = 3,  // Tier F — bidirectional elaboration (2 dims)
    Versioned    = 4,  // Tier V — consistency check at each site (1 dim)
};

inline constexpr std::size_t TIER_KIND_COUNT =
    std::meta::enumerators_of(^^TierKind).size();

[[nodiscard]] constexpr std::string_view tier_kind_name(TierKind t) noexcept {
    switch (t) {
        case TierKind::Semiring:     return "Tier-S (Semiring)";
        case TierKind::Lattice:      return "Tier-L (Lattice)";
        case TierKind::Typestate:    return "Tier-T (Typestate)";
        case TierKind::Foundational: return "Tier-F (Foundational)";
        case TierKind::Versioned:    return "Tier-V (Versioned)";
        default:                     return std::string_view{"<unknown TierKind>"};
    }
}

// ═════════════════════════════════════════════════════════════════════
// ── DimensionAxis — the 20 dimensions per fixy.md §24.1 ────────────
// ═════════════════════════════════════════════════════════════════════
//
// ORDER must match fixy.md §24.1 reading order; APPEND-ONLY.  The
// FX-numbered comment beside each enumerator preserves the source
// dimension number from FX's 22-dim catalog (dims 12 Clock Domain
// and 17 FP Order are dropped per fixy.md §24.1; their absence
// here is structural, not accidental).

enum class DimensionAxis : std::uint8_t {
    Type           = 0,   // F  (FX dim 1)
    Refinement     = 1,   // F  (FX dim 2)
    Usage          = 2,   // S  (FX dim 3)
    Effect         = 3,   // S  (FX dim 4)
    Security       = 4,   // S  (FX dim 5)
    Protocol       = 5,   // T  (FX dim 6)
    Lifetime       = 6,   // S  (FX dim 7)
    Provenance     = 7,   // S  (FX dim 8)
    Trust          = 8,   // S  (FX dim 9)
    Representation = 9,   // L  (FX dim 10)
    Observability  = 10,  // S  (FX dim 11)
    // FX dim 12 Clock Domain dropped per fixy.md §24.1 — Crucible
    // does not synthesize Verilog.
    Complexity     = 11,  // S  (FX dim 13)
    Precision      = 12,  // S  (FX dim 14)
    Space          = 13,  // S  (FX dim 15)
    Overflow       = 14,  // S  (FX dim 16)
    // FX dim 17 FP Order dropped per fixy.md §24.1 — NumericalRecipe
    // pinning at the Mimic-emit layer subsumes it.
    Mutation       = 15,  // S  (FX dim 18)
    Reentrancy     = 16,  // S  (FX dim 19)
    Size           = 17,  // S  (FX dim 20)
    Version        = 18,  // V  (FX dim 21)
    Staleness      = 19,  // S  (FX dim 22)
    // Synchronization (fixy-A3-008) — added 2026-05-18 to host
    // safety::Wait (SpinPause/Sleep/Futex strategy) and
    // safety::MemOrder (Relaxed/Acquire/Release/SeqCst) wrappers,
    // both previously misclassified on dim 16 Reentrancy.  The
    // Reentrancy axis tracks call-graph self-call (`rec`/`with
    // Reentrant`); Wait + MemOrder are synchronization-discipline
    // annotations whose semantics do not overlap with reentrancy.
    // Tier S with par=join (strictest-wins) reading; no Fn<>
    // aggregator slot (the wrappers compose at use-sites, not via
    // Fn parameter aggregation — parallel to dim 11 Observability
    // which is also derived rather than Fn-aggregated).
    Synchronization = 20, // S  (Crucible extension, 2026-05-18)
    // Regime (fixy-A3-009) — added 2026-05-18 to host
    // safety::HotPath (Hot/Warm/Cold tier) wrappers, previously
    // misclassified on dim 13 Complexity.  Complexity tracks
    // asymptotic / termination-class bounds (which Progress
    // legitimately occupies — Terminating / Productive /
    // Possibly_Diverging); HotPath tracks *operating regime* —
    // where in the latency-budget hierarchy a function lives.
    // Tier S with par=join (hottest-wins; Hot ⊕ Warm = Hot).  No
    // Fn<> aggregator slot — HotPath composes at use-sites, like
    // Observability and Synchronization.
    Regime          = 21, // S  (Crucible extension, 2026-05-18)
    // FpMode (FIXY-V-088) — added 2026-05-22 to host the 11-sub-axis
    // floating-point mode taxonomy (Rounding / Ftz / Contract /
    // TrapMask / Denormal / NanPolicy / InfPolicy / ComplexLayout /
    // LibmPolicy / Reassociate / FpConstant) that V-089/V-090 will
    // populate.  Previously these would have been silently folded onto
    // Precision (which tracks element-type precision: FP32/FP16/BF16/
    // E4M3/E5M2), but FP-mode is structurally orthogonal — same
    // FP32 element type produces bit-different results under different
    // rounding/Ftz/contract modes.  Pinning FpMode at the axis level
    // makes Merkle-hash-safe FP canonicalization (V-093) tractable.
    // Tier S with par=join (strictest-wins) reading — composing two
    // call sites' FP-mode admits only the intersection of their
    // tolerances; no Fn<> aggregator slot (composes via wrapper-nesting
    // and Forge phase E.RecipeSelect, parallel to Synchronization and
    // Regime).
    FpMode          = 22, // S  (Crucible extension, 2026-05-22)
    // SyscallSurface (FIXY-V-097) — added 2026-05-22 to host the
    // syscall-family taxonomy (V-098 ships the per-family grant catalog,
    // V-099 ships the per-ioctl grants, V-100 ships the syscall→effect-row
    // bridge).  Previously a function's "what kernel surface does this
    // touch" was implicit in the effect row (`effects::IO` / `Block` /
    // `Alloc`) which collapses files / network / mmap / process-control
    // into a single bit — too coarse to drive (a) Forge phase E.RecipeSelect
    // hot-path admission (NoSyscall vs VdsoOnly vs everything-else),
    // (b) Mimic per-vendor backend gates that need to know whether a
    // kernel emits direct ioctls, (c) Cipher cold-tier paths that must
    // declare FileMutation vs Memory-mapping, and (d) Canopy peer-RX
    // paths that must declare NetworkIo separately from ProcessControl.
    // Pinning SyscallSurface as its own axis makes those gates expressible
    // — V-098's grant catalog will route per-family tags here.
    //
    // Tier S with par=join (strictest-wins) reading along the chain
    // NoSyscall ⊏ VdsoOnly ⊏ ReadOnlyState ⊏ FileMutation ⊏ MemoryMapping
    // ⊏ ThreadSync ⊏ NetworkIo ⊏ ProcessControl ⊏ Privilege; composition
    // of two sites' syscall surfaces is the JOIN (the larger family),
    // matching subset-inclusion semantics on the underlying syscall set.
    // No Fn<> aggregator slot (composes via wrapper-nesting at the value
    // site + Forge phase E gating, parallel to Synchronization / Regime
    // / FpMode).
    SyscallSurface  = 23, // S  (Crucible extension, 2026-05-22)
    // ControlFlow / CallShape / StackUse / GlobalState / Stdio
    // (FIXY-V-238) — added 2026-05-23 to host the function-behavior
    // taxonomy that V-239/V-240/V-241 will populate (ControlFlowLattice
    // Pure ⊏ AbortOnly ⊏ ThrowOnly ⊏ MayLongjmp ⊏ MaySignal;
    // CallShapeLattice Direct ⊏ BoundedRecurses<N> ⊏ Indirect ⊏ Virtual
    // ⊏ Unbounded; plus StackUse / GlobalState / Stdio chains).  These
    // were previously implicit: control-flow escapes folded into the
    // Effect row (`Block`), call shape was invisible, stack/global/stdio
    // surfaces had no axis at all — too coarse to drive (a) permission_
    // fork's no-throw requirement (V-087 already rejects grant::ctrl::
    // throws structurally; ControlFlow makes it an axis), (b) Forge
    // hot-path admission gates on bounded call shape, (c) the §6.8
    // C001/D001/D002/G001/L006/P003/S001/S004 collision family (V-243),
    // and (d) Meyers-singleton init-cycle detection (V-248).  All five
    // are Tier-S with par=join (strictest-wins) along their chains; no
    // Fn<> aggregator slot (compose via wrapper-nesting at the value
    // site + grant engagement, parallel to Synchronization / Regime /
    // FpMode / SyscallSurface).
    ControlFlow     = 24, // S  (Crucible extension, 2026-05-23)
    CallShape       = 25, // S  (Crucible extension, 2026-05-23)
    StackUse        = 26, // S  (Crucible extension, 2026-05-23)
    GlobalState     = 27, // S  (Crucible extension, 2026-05-23)
    Stdio           = 28, // S  (Crucible extension, 2026-05-23)
    // HwInstruction / BarrierStrength / SimdIsa (FIXY-V-253, Agent 11
    // §3.2) — added 2026-05-23 to host the hardware-instruction taxonomy
    // that V-251/V-252/V-250 populate and V-254/V-255/V-256 wrap:
    //   HwInstruction  — HwInstructionLattice NoneAllowed ⊏ Scalar ⊏
    //                    Vectorizable ⊏ NonDeterministicTsc ⊏ PrivilegedMsr;
    //                    what instruction CLASS a kernel may issue (the
    //                    Tier-0 Mimic blocker — Mimic must know per kernel
    //                    whether SIMD / rdtsc / ring-0 MSR are emitted).
    //   BarrierStrength— BarrierStrengthLattice None ⊏ CompilerBarrier ⊏
    //                    AcquireLoad ⊏ ReleaseStore ⊏ AcqRel ⊏ SeqCst ⊏
    //                    FullFence; what fence strength a boundary requires
    //                    (distinct from the Synchronization axis's MemOrder
    //                    tag — this is the standalone HW-fence ladder).
    //   SimdIsa        — SimdIsaLattice, a Tier-L NON-DISTRIBUTIVE partial
    //                    order (x86 trunk × ARM trunk joined only at
    //                    Scalar / Portable); which ISA-extension the host
    //                    must provide for a compiled SIMD kernel to issue.
    // HwInstruction / BarrierStrength are Tier-S with par=join (strictest-
    // wins along their chains, parallel to ControlFlow / CallShape);
    // SimdIsa is Tier-L (the second Tier-L axis, peer to Representation).
    // No Fn<> aggregator slot (compose via wrapper-nesting at the value
    // site + grant engagement).
    HwInstruction   = 29, // S  (Crucible extension, 2026-05-23)
    BarrierStrength = 30, // S  (Crucible extension, 2026-05-23)
    SimdIsa         = 31, // L  (Crucible extension, 2026-05-23)
};

inline constexpr std::size_t DIMENSION_AXIS_COUNT =
    std::meta::enumerators_of(^^DimensionAxis).size();

[[nodiscard]] constexpr std::string_view dimension_axis_name(DimensionAxis d) noexcept {
    switch (d) {
        case DimensionAxis::Type:           return "Type";
        case DimensionAxis::Refinement:     return "Refinement";
        case DimensionAxis::Usage:          return "Usage";
        case DimensionAxis::Effect:         return "Effect";
        case DimensionAxis::Security:       return "Security";
        case DimensionAxis::Protocol:       return "Protocol";
        case DimensionAxis::Lifetime:       return "Lifetime";
        case DimensionAxis::Provenance:     return "Provenance";
        case DimensionAxis::Trust:          return "Trust";
        case DimensionAxis::Representation: return "Representation";
        case DimensionAxis::Observability:  return "Observability";
        case DimensionAxis::Complexity:     return "Complexity";
        case DimensionAxis::Precision:      return "Precision";
        case DimensionAxis::Space:          return "Space";
        case DimensionAxis::Overflow:       return "Overflow";
        case DimensionAxis::Mutation:       return "Mutation";
        case DimensionAxis::Reentrancy:     return "Reentrancy";
        case DimensionAxis::Size:           return "Size";
        case DimensionAxis::Version:        return "Version";
        case DimensionAxis::Staleness:      return "Staleness";
        case DimensionAxis::Synchronization: return "Synchronization";
        case DimensionAxis::Regime:         return "Regime";
        case DimensionAxis::FpMode:         return "FpMode";
        case DimensionAxis::SyscallSurface: return "SyscallSurface";
        case DimensionAxis::ControlFlow:    return "ControlFlow";
        case DimensionAxis::CallShape:      return "CallShape";
        case DimensionAxis::StackUse:       return "StackUse";
        case DimensionAxis::GlobalState:    return "GlobalState";
        case DimensionAxis::Stdio:          return "Stdio";
        case DimensionAxis::HwInstruction:  return "HwInstruction";
        case DimensionAxis::BarrierStrength: return "BarrierStrength";
        case DimensionAxis::SimdIsa:        return "SimdIsa";
        default:                            return std::string_view{"<unknown DimensionAxis>"};
    }
}

// ─── tier_of_axis — fixy.md §24.1 hard-coded mapping ───────────────
//
// SOURCE OF TRUTH for which Tier each dimension uses.  Specializing
// this is a fixy.md design change, not a substrate change — every
// production caller routes here for the dim → Tier mapping.

[[nodiscard]] constexpr TierKind tier_of_axis(DimensionAxis d) noexcept {
    switch (d) {
        case DimensionAxis::Type:
        case DimensionAxis::Refinement:
            return TierKind::Foundational;

        case DimensionAxis::Protocol:
            return TierKind::Typestate;

        case DimensionAxis::Representation:
        case DimensionAxis::SimdIsa:
            return TierKind::Lattice;

        case DimensionAxis::Version:
            return TierKind::Versioned;

        case DimensionAxis::Usage:
        case DimensionAxis::Effect:
        case DimensionAxis::Security:
        case DimensionAxis::Lifetime:
        case DimensionAxis::Provenance:
        case DimensionAxis::Trust:
        case DimensionAxis::Observability:
        case DimensionAxis::Complexity:
        case DimensionAxis::Precision:
        case DimensionAxis::Space:
        case DimensionAxis::Overflow:
        case DimensionAxis::Mutation:
        case DimensionAxis::Reentrancy:
        case DimensionAxis::Size:
        case DimensionAxis::Staleness:
        case DimensionAxis::Synchronization:
        case DimensionAxis::Regime:
        case DimensionAxis::FpMode:
        case DimensionAxis::SyscallSurface:
        case DimensionAxis::ControlFlow:
        case DimensionAxis::CallShape:
        case DimensionAxis::StackUse:
        case DimensionAxis::GlobalState:
        case DimensionAxis::Stdio:
        case DimensionAxis::HwInstruction:
        case DimensionAxis::BarrierStrength:
            return TierKind::Semiring;

        default:
            // Unreachable per the exhaustive switch + DIMENSION_AXIS_COUNT
            // self-test, but every path must return.  Returning Semiring as
            // the fallthrough would silently mis-classify new axes; instead
            // we return a value whose name() flags the leak in diagnostics.
            return TierKind{0xFF};
    }
}

template <DimensionAxis D>
inline constexpr TierKind tier_of_axis_v = tier_of_axis(D);

// ═════════════════════════════════════════════════════════════════════
// ── 5 Tier concept families ─────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Each concept characterizes the STRUCTURAL SHAPE a grade type has
// when used at its respective Tier.  Concepts overlap by design.
// The Tier classification per dimension comes from `tier_of_axis`,
// not from concept-satisfaction detection.

// SemiringGrade — Tier S grades carry semiring composition (add/mul,
// zero/one) on top of the lattice carrier.  Used at par/seq sites
// where + and · are the composition operations.
template <typename G>
concept SemiringGrade = algebra::Lattice<G> && algebra::Semiring<G>;

// LatticeGrade — Tier L grades carry lattice composition (join/meet)
// without requiring semiring add/mul.  Used at par/seq sites where
// par=join and seq=meet, with a per-element validity predicate
// (valid_D check) checked on every composition.
template <typename G>
concept LatticeGrade = algebra::Lattice<G>;

// TypestateGrade — Tier T grades are session-protocol types whose
// composition is transition-based (no par/seq lattice algebra).
// Detected via dual exposure of `state_type` and `transition_type`,
// the convention used pervasively by sessions/Session.h.  A type
// that merely looks lattice-shaped will not satisfy this; sessions
// are deliberately NOT graded (see Safety.h umbrella).
template <typename G>
concept TypestateGrade = requires {
    typename G::state_type;
    typename G::transition_type;
};

// FoundationalGrade — Tier F grades cover dim 1 (Type) and dim 2
// (Refinement).  Bare types satisfy this (any T is a foundational
// grade for the Type dimension).  Refinement predicates additionally
// ship `static constexpr bool check(value)`; both shapes admit here.
// Per-dimension narrower concepts (e.g., Refined predicate gates)
// further discriminate downstream.
template <typename G>
concept FoundationalGrade = std::is_object_v<G>;

// VersionedGrade — Tier V grades carry a compatibility predicate
// between version values.  Required for Tier V composition: at each
// par/seq site the runtime checks compatible(prev, next) before
// admitting the new grade.
template <typename G>
concept VersionedGrade = requires {
    typename G::element_type;
} && requires (typename G::element_type a, typename G::element_type b) {
    { G::compatible(a, b) } -> std::convertible_to<bool>;
};

// ═════════════════════════════════════════════════════════════════════
// ── tier_for_grade — best-effort Tier classification of a grade ────
// ═════════════════════════════════════════════════════════════════════
//
// Default rule, in priority order:
//   1. TypestateGrade  → Tier T
//   2. VersionedGrade  → Tier V
//   3. SemiringGrade   → Tier S  (every Semiring is also a Lattice;
//                                  the Semiring discriminator wins)
//   4. LatticeGrade    → Tier L  (pure Lattice without Semiring)
//   5. otherwise        → Tier F  (Type / Refinement catch-all)
//
// CALLERS should prefer `tier_of_axis(D)` when the dimension D is
// known.  This trait is for cases where only the grade type is
// available and the dimension classification is ambiguous.  A
// per-grade specialization of `tier_for_grade<G>` overrides the
// default rule.

template <typename G>
struct tier_for_grade {
    static constexpr TierKind value = []() consteval {
        if constexpr (TypestateGrade<G>)        return TierKind::Typestate;
        else if constexpr (VersionedGrade<G>)   return TierKind::Versioned;
        else if constexpr (SemiringGrade<G>)    return TierKind::Semiring;
        else if constexpr (LatticeGrade<G>)     return TierKind::Lattice;
        else                                    return TierKind::Foundational;
    }();
};

template <typename G>
inline constexpr TierKind tier_for_grade_v = tier_for_grade<G>::value;

// ─── dimension_tier — Tier classification of a GradedWrapper ──────
//
// For wrappers conforming to GradedWrapper, the Tier comes from the
// substrate's lattice_type per the heuristic above.  This is the
// "best-effort" path; per-wrapper exact Tier (matching fixy.md §24.1
// hard-coded mapping for the dim the wrapper covers) ships when the
// wrapper specializes its dimension via the future `wrapper_dimension`
// trait (Phase 1 P1-N).

template <algebra::GradedWrapper W>
inline constexpr TierKind dimension_tier_v =
    tier_for_grade_v<typename W::lattice_type>;

// ═════════════════════════════════════════════════════════════════════
// ── wrapper_dimension / verify_quadruple — exact wrapper table ──────
// ═════════════════════════════════════════════════════════════════════
//
// `dimension_tier_v<W>` above is intentionally heuristic: it derives a
// Tier from the lattice carrier's structural concept shape.  GAPS-091
// needs the inverse discipline: each shipped Graded-backed wrapper names
// the dimension it is meant to carry, then a consteval verifier checks
// the wrapper's (lattice, modality, tier) surface against that explicit
// declaration.  This table is deliberately small and exact; adding a new
// Graded-backed wrapper means adding one specialization here.

template <typename W>
struct wrapper_dimension;

template <typename W>
concept DimensionedGradedWrapper =
    algebra::GradedWrapper<std::remove_cvref_t<W>> &&
    requires { wrapper_dimension<std::remove_cvref_t<W>>::value; };

template <DimensionedGradedWrapper W>
inline constexpr DimensionAxis wrapper_dimension_v =
    wrapper_dimension<std::remove_cvref_t<W>>::value;

template <DimensionedGradedWrapper W>
inline constexpr TierKind wrapper_tier_v =
    tier_of_axis(wrapper_dimension_v<W>);

template <DimensionedGradedWrapper W>
using wrapper_lattice_t = typename std::remove_cvref_t<W>::lattice_type;

template <DimensionedGradedWrapper W>
inline constexpr algebra::ModalityKind wrapper_modality_v =
    std::remove_cvref_t<W>::modality;

template <typename T>
struct wrapper_dimension<Linear<T>>
    : std::integral_constant<DimensionAxis, DimensionAxis::Usage> {};

template <auto Pred, typename T>
struct wrapper_dimension<Refined<Pred, T>>
    : std::integral_constant<DimensionAxis, DimensionAxis::Refinement> {};

template <auto Pred, typename T>
struct wrapper_dimension<SealedRefined<Pred, T>>
    : std::integral_constant<DimensionAxis, DimensionAxis::Refinement> {};

template <typename T, typename Tag>
struct wrapper_dimension<Tagged<T, Tag>>
    : std::integral_constant<DimensionAxis, DimensionAxis::Provenance> {};

template <typename T>
struct wrapper_dimension<Secret<T>>
    : std::integral_constant<DimensionAxis, DimensionAxis::Security> {};

template <typename T>
struct wrapper_dimension<Stale<T>>
    : std::integral_constant<DimensionAxis, DimensionAxis::Staleness> {};

template <typename T, std::size_t N, typename Tag>
struct wrapper_dimension<TimeOrdered<T, N, Tag>>
    : std::integral_constant<DimensionAxis, DimensionAxis::Representation> {};

template <typename T, typename Cmp>
struct wrapper_dimension<Monotonic<T, Cmp>>
    : std::integral_constant<DimensionAxis, DimensionAxis::Mutation> {};

template <typename T, template <typename...> class Storage>
struct wrapper_dimension<AppendOnly<T, Storage>>
    : std::integral_constant<DimensionAxis, DimensionAxis::Mutation> {};

// fixy-A3-009 (2026-05-18): HotPath reclassified from dim 13
// Complexity to dim 21 Regime.  Complexity tracks asymptotic /
// termination-class bounds (where Progress legitimately lives:
// Terminating / Productive / Possibly_Diverging); HotPath tracks
// *operating regime* — where in the latency budget a function
// lives (Hot / Warm / Cold).  Neither axis subsumes the other:
// a Cold-regime function can still be Terminating, and a Hot-
// regime function can still be Possibly_Diverging (and is then
// a bug in the Hot path that must be caught by other gates).
template <HotPathTier_v Tier, typename T>
struct wrapper_dimension<HotPath<Tier, T>>
    : std::integral_constant<DimensionAxis, DimensionAxis::Regime> {};

template <DetSafeTier_v Tier, typename T>
struct wrapper_dimension<DetSafe<Tier, T>>
    : std::integral_constant<DimensionAxis, DimensionAxis::Effect> {};

template <Tolerance Tier, typename T>
struct wrapper_dimension<NumericalTier<Tier, T>>
    : std::integral_constant<DimensionAxis, DimensionAxis::Precision> {};

template <VendorBackend_v Backend, typename T>
struct wrapper_dimension<Vendor<Backend, T>>
    : std::integral_constant<DimensionAxis, DimensionAxis::Representation> {};

// FIXY-V-254 — Hw<HwInstruction Tier, T> occupies the HwInstruction axis
// (V-253, Tier-S Semiring with par=join).  §XVI neighborhood: between
// Vendor (which backend) and ResidencyHeat (where the value lives).
template <HwInstruction_v Tier, typename T>
struct wrapper_dimension<Hw<Tier, T>>
    : std::integral_constant<DimensionAxis, DimensionAxis::HwInstruction> {};

// FIXY-V-255 — BarrierGuarded<BarrierStrength Tier, T> occupies the
// BarrierStrength axis (V-253, Tier-S Semiring).  Repr-neighborhood peer
// to Hw; the publication-fence tier the value was released under.
template <BarrierStrength_v Tier, typename T>
struct wrapper_dimension<BarrierGuarded<Tier, T>>
    : std::integral_constant<DimensionAxis, DimensionAxis::BarrierStrength> {};

template <ResidencyHeatTag_v Tier, typename T>
struct wrapper_dimension<ResidencyHeat<Tier, T>>
    : std::integral_constant<DimensionAxis, DimensionAxis::Space> {};

template <CipherTierTag_v Tier, typename T>
struct wrapper_dimension<CipherTier<Tier, T>>
    : std::integral_constant<DimensionAxis, DimensionAxis::Security> {};

template <AllocClassTag_v Tag, typename T>
struct wrapper_dimension<AllocClass<Tag, T>>
    : std::integral_constant<DimensionAxis, DimensionAxis::Space> {};

// fixy-A3-008 (2026-05-18): Wait + MemOrder reclassified from
// dim 16 Reentrancy to dim 20 Synchronization.  Reentrancy tracks
// call-graph self-call ("rec" / "with Reentrant"); Wait tracks
// waiting-strategy choice (SpinPause / Sleep / Futex) and MemOrder
// tracks C++ memory-order discipline (Relaxed / Acquire / Release
// / SeqCst).  Both are concurrency-coordination axes — neither
// touches reentrancy semantics.  Tier preserved at Semiring (S).
template <WaitStrategy_v Strategy, typename T>
struct wrapper_dimension<Wait<Strategy, T>>
    : std::integral_constant<DimensionAxis, DimensionAxis::Synchronization> {};

template <MemOrderTag_v Tag, typename T>
struct wrapper_dimension<MemOrder<Tag, T>>
    : std::integral_constant<DimensionAxis, DimensionAxis::Synchronization> {};

template <ProgressClass_v Class, typename T>
struct wrapper_dimension<Progress<Class, T>>
    : std::integral_constant<DimensionAxis, DimensionAxis::Complexity> {};

template <Consistency_v Level, typename T>
struct wrapper_dimension<Consistency<Level, T>>
    : std::integral_constant<DimensionAxis, DimensionAxis::Version> {};

// FIXY-V-054 — Witness<Tier, T> occupies the Observability axis.
// Witness encodes epistemic confidence (UNWITNESSED ⊑ TYPE_CHECKED ⊑
// TEST_PASSED ⊑ FORMALLY_VERIFIED) — i.e. the OBSERVABLE strength of
// evidence the producer had for the value's invariant.  The
// Observability axis was previously unoccupied (FX dim 11); Witness
// is its canonical inhabitant.  Tier-S (Semiring) per tier_of_axis.
template <Witness_v Tier, typename T>
struct wrapper_dimension<Witness<Tier, T>>
    : std::integral_constant<DimensionAxis, DimensionAxis::Observability> {};

// FIXY-V-079 — JoinPolicy<Tier, T> occupies the Synchronization axis
// (dim 20).  JoinPolicy encodes structural-concurrency engagement
// (FORGET ⊏ DETACH ⊏ ABANDON ⊏ CANCEL ⊏ WAIT_DEADLINE ⊏ JOIN_ALL) —
// i.e. the SYNCHRONIZATION DISCIPLINE the parent applied to its
// spawned children.  Shares the axis with Wait (queue-side readiness
// sync) and MemOrder (memory-side ordering sync), all three being
// concurrency-discipline annotations; tier-S with par=join
// (strictest-wins) reading per the same shape as Wait + MemOrder.
template <JoinPolicy_v Tier, typename T>
struct wrapper_dimension<JoinPolicy<Tier, T>>
    : std::integral_constant<DimensionAxis, DimensionAxis::Synchronization> {};

// FIXY-V-090 — FpModePinned<auto Mode, T> on the FpMode axis (Tier-S
// chain, axis 22).  Every per-axis spelling (FpRoundingPinned /
// FpFtzPinned / ... / FpConstantRoundingPinned) instantiates the same
// FpModePinned class template with a different NTTP enum type, so ONE
// generic spec catches all 11 instantiations.  The downstream consumer
// (federation cache routing, dimension-traits queries) gets the same
// DimensionAxis::FpMode for every sub-axis; per-axis disambiguation
// happens through the row_hash specializations in safety/diag/
// RowHashFold.h (salts 0x21..0x2B per NTTP enum type).
template <auto Mode, typename T>
struct wrapper_dimension<FpModePinned<Mode, T>>
    : std::integral_constant<DimensionAxis, DimensionAxis::FpMode> {};

template <Lifetime_v Scope, typename T>
struct wrapper_dimension<OpaqueLifetime<Scope, T>>
    : std::integral_constant<DimensionAxis, DimensionAxis::Lifetime> {};

template <CrashClass_v Class, typename T>
struct wrapper_dimension<Crash<Class, T>>
    : std::integral_constant<DimensionAxis, DimensionAxis::Effect> {};

template <typename T>
struct wrapper_dimension<Budgeted<T>>
    : std::integral_constant<DimensionAxis, DimensionAxis::Space> {};

template <typename T>
struct wrapper_dimension<EpochVersioned<T>>
    : std::integral_constant<DimensionAxis, DimensionAxis::Version> {};

template <typename T>
struct wrapper_dimension<NumaPlacement<T>>
    : std::integral_constant<DimensionAxis, DimensionAxis::Representation> {};

template <typename T>
struct wrapper_dimension<RecipeSpec<T>>
    : std::integral_constant<DimensionAxis, DimensionAxis::Precision> {};

template <TierKind Tier, typename Lattice>
[[nodiscard]] consteval bool tier_admits_lattice() noexcept {
    if constexpr (Tier == TierKind::Typestate) {
        return TypestateGrade<Lattice>;
    } else if constexpr (Tier == TierKind::Foundational) {
        return true;
    } else {
        // Tier-S / Tier-L / Tier-V wrappers in the current safety tree
        // are all lattice-backed at minimum.  Tier-S semiring laws are
        // enforced by the specific wrapper semantics where the carrier is
        // a type-level singleton or product lattice rather than by
        // requiring every grade carrier to publish add/mul.
        return LatticeGrade<Lattice>;
    }
}

template <TierKind, algebra::ModalityKind Modality>
[[nodiscard]] consteval bool tier_admits_modality() noexcept {
    return algebra::IsModality<Modality>;
}

template <DimensionedGradedWrapper W>
[[nodiscard]] consteval bool verify_quadruple() noexcept {
    using X = std::remove_cvref_t<W>;
    using L = wrapper_lattice_t<X>;
    constexpr auto tier = wrapper_tier_v<X>;
    constexpr auto modality = wrapper_modality_v<X>;

    return std::is_same_v<L, typename X::lattice_type>
        && std::is_same_v<L, typename X::graded_type::lattice_type>
        && modality == X::modality
        && modality == algebra::graded_modality_v<typename X::graded_type>
        && tier_kind_name(tier) != std::string_view{"<unknown TierKind>"}
        && tier_admits_lattice<tier, L>()
        && tier_admits_modality<tier, modality>();
}

// ═════════════════════════════════════════════════════════════════════
// ── Self-test (compile-time + reflection-driven coverage) ──────────
// ═════════════════════════════════════════════════════════════════════

namespace detail::dimension_traits_self_test {

// ── Cardinality assertions ─────────────────────────────────────────
static_assert(TIER_KIND_COUNT == 5,
    "TierKind catalog diverged from fixy.md §24.1 Tier S/L/T/F/V (5); "
    "if intentional, update fixy.md and this constant together.");
static_assert(DIMENSION_AXIS_COUNT == 32,
    "DimensionAxis catalog diverged from fixy.md §24.1 (32 dims: FX's "
    "22 minus dim 12 Clock Domain and dim 17 FP Order, plus the Crucible "
    "Synchronization extension added 2026-05-18 for Wait + MemOrder, plus "
    "the Crucible Regime extension added 2026-05-18 for HotPath, plus "
    "the Crucible FpMode extension added 2026-05-22 for the 11-sub-axis "
    "FP-mode taxonomy per FIXY-V-088, plus the Crucible SyscallSurface "
    "extension added 2026-05-22 for the syscall-family taxonomy per "
    "FIXY-V-097, plus the five Crucible function-behavior extensions added "
    "2026-05-23 (ControlFlow / CallShape / StackUse / GlobalState / Stdio) "
    "per FIXY-V-238, plus the three Crucible hardware-instruction "
    "extensions added 2026-05-23 (HwInstruction / BarrierStrength / "
    "SimdIsa) per FIXY-V-253); if intentional, update fixy.md §24.1 + "
    "§24.14 + §24.15 + §24.16 + §24.17 + §24.18 + §24.19 and this "
    "constant.");

// ── Reflection-driven name coverage (TierKind) ─────────────────────
[[nodiscard]] consteval bool every_tier_kind_has_name() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^TierKind));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        const auto n = tier_kind_name([:en:]);
        if (n == std::string_view{"<unknown TierKind>"}) return false;
        if (n.empty())                                   return false;
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_tier_kind_has_name(),
    "tier_kind_name() missing arm for at least one TierKind — add the "
    "arm or the new tier leaks the '<unknown TierKind>' sentinel.");

// ── Reflection-driven name coverage (DimensionAxis) ────────────────
[[nodiscard]] consteval bool every_dimension_axis_has_name() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^DimensionAxis));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        const auto n = dimension_axis_name([:en:]);
        if (n == std::string_view{"<unknown DimensionAxis>"}) return false;
        if (n.empty())                                        return false;
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_dimension_axis_has_name(),
    "dimension_axis_name() missing arm for at least one DimensionAxis — "
    "add the arm or the new axis leaks the '<unknown DimensionAxis>' "
    "sentinel.");

// ── Reflection-driven Tier coverage (every axis maps to a Tier) ────
[[nodiscard]] consteval bool every_dimension_axis_has_tier() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^DimensionAxis));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        const auto t = tier_of_axis([:en:]);
        // tier_of_axis returns TierKind{0xFF} on unreachable
        // fallthrough; the name resolves to "<unknown TierKind>".
        if (tier_kind_name(t) == std::string_view{"<unknown TierKind>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_dimension_axis_has_tier(),
    "tier_of_axis() switch missing arm for at least one DimensionAxis — "
    "add the arm or new axes silently fall through to the unreachable "
    "TierKind{0xFF} sentinel.");

// ── fixy.md §24.1 hard-coded Tier counts ──────────────────────────
[[nodiscard]] consteval std::size_t count_dims_in_tier(TierKind t) noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^DimensionAxis));
    std::size_t n = 0;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (tier_of_axis([:en:]) == t) ++n;
    }
#pragma GCC diagnostic pop
    return n;
}

static_assert(count_dims_in_tier(TierKind::Semiring)     == 26,
    "fixy.md §24.1 declares 26 Tier-S dimensions (15 FX-inherited + "
    "Synchronization 2026-05-18 per fixy-A3-008 + Regime 2026-05-18 per "
    "fixy-A3-009 + FpMode 2026-05-22 per FIXY-V-088 + SyscallSurface "
    "2026-05-22 per FIXY-V-097 + ControlFlow / CallShape / StackUse / "
    "GlobalState / Stdio 2026-05-23 per FIXY-V-238 + HwInstruction / "
    "BarrierStrength 2026-05-23 per FIXY-V-253); tier_of_axis "
    "disagrees.");
static_assert(count_dims_in_tier(TierKind::Lattice)      == 2,
    "fixy.md §24.1 declares 2 Tier-L dimensions (Representation + SimdIsa "
    "2026-05-23 per FIXY-V-253); tier_of_axis disagrees.");
static_assert(count_dims_in_tier(TierKind::Typestate)    == 1,
    "fixy.md §24.1 declares 1 Tier-T dimension (Protocol); "
    "tier_of_axis disagrees.");
static_assert(count_dims_in_tier(TierKind::Foundational) == 2,
    "fixy.md §24.1 declares 2 Tier-F dimensions (Type, Refinement); "
    "tier_of_axis disagrees.");
static_assert(count_dims_in_tier(TierKind::Versioned)    == 1,
    "fixy.md §24.1 declares 1 Tier-V dimension (Version); "
    "tier_of_axis disagrees.");

// Sum check — every dim assigned to exactly one Tier.
static_assert(count_dims_in_tier(TierKind::Semiring)
            + count_dims_in_tier(TierKind::Lattice)
            + count_dims_in_tier(TierKind::Typestate)
            + count_dims_in_tier(TierKind::Foundational)
            + count_dims_in_tier(TierKind::Versioned)
              == DIMENSION_AXIS_COUNT,
    "Sum of per-Tier dimension counts does not equal DIMENSION_AXIS_COUNT "
    "— a dimension is either uncounted or double-counted in tier_of_axis.");

// ── Concept witnesses ──────────────────────────────────────────────
//
// Trivial in-house witnesses for each Tier concept; exercises the
// concepts WITHOUT pulling in the full algebra/lattices/* tree.

struct TestLattice {
    using element_type = bool;
    [[nodiscard]] static constexpr bool bottom() noexcept { return false; }
    [[nodiscard]] static constexpr bool top()    noexcept { return true;  }
    [[nodiscard]] static constexpr bool leq (bool a, bool b) noexcept { return !a || b; }
    [[nodiscard]] static constexpr bool join(bool a, bool b) noexcept { return a || b; }
    [[nodiscard]] static constexpr bool meet(bool a, bool b) noexcept { return a && b; }
};

struct TestSemiring {
    using element_type = bool;
    [[nodiscard]] static constexpr bool bottom() noexcept { return false; }
    [[nodiscard]] static constexpr bool top()    noexcept { return true;  }
    [[nodiscard]] static constexpr bool leq (bool a, bool b) noexcept { return !a || b; }
    [[nodiscard]] static constexpr bool join(bool a, bool b) noexcept { return a || b; }
    [[nodiscard]] static constexpr bool meet(bool a, bool b) noexcept { return a && b; }
    [[nodiscard]] static constexpr bool zero() noexcept { return false; }
    [[nodiscard]] static constexpr bool one()  noexcept { return true;  }
    [[nodiscard]] static constexpr bool add(bool a, bool b) noexcept { return a || b; }
    [[nodiscard]] static constexpr bool mul(bool a, bool b) noexcept { return a && b; }
};

struct TestVersioned {
    using element_type = std::uint32_t;
    [[nodiscard]] static constexpr bool compatible(std::uint32_t a, std::uint32_t b) noexcept {
        return a == b;
    }
};

struct TestTypestate {
    using state_type      = int;
    using transition_type = int;
};

struct TestBareFoundational {
    int payload{0};
};

// Concept satisfaction.
static_assert( LatticeGrade<TestLattice>);
static_assert(!SemiringGrade<TestLattice>);   // No add/mul.

static_assert( LatticeGrade<TestSemiring>);
static_assert( SemiringGrade<TestSemiring>);  // Both shapes.

static_assert( VersionedGrade<TestVersioned>);
static_assert(!LatticeGrade<TestVersioned>);
static_assert(!TypestateGrade<TestVersioned>);

static_assert( TypestateGrade<TestTypestate>);
static_assert(!LatticeGrade<TestTypestate>);
static_assert(!VersionedGrade<TestTypestate>);

static_assert(FoundationalGrade<int>);
static_assert(FoundationalGrade<TestBareFoundational>);

// tier_for_grade priority order.
static_assert(tier_for_grade_v<TestSemiring>          == TierKind::Semiring);
static_assert(tier_for_grade_v<TestLattice>           == TierKind::Lattice);
static_assert(tier_for_grade_v<TestTypestate>         == TierKind::Typestate);
static_assert(tier_for_grade_v<TestVersioned>         == TierKind::Versioned);
static_assert(tier_for_grade_v<TestBareFoundational>  == TierKind::Foundational);
static_assert(tier_for_grade_v<int>                   == TierKind::Foundational);

// Diagnostic surface — exact strings.
static_assert(tier_kind_name(TierKind::Semiring)     == "Tier-S (Semiring)");
static_assert(tier_kind_name(TierKind::Lattice)      == "Tier-L (Lattice)");
static_assert(tier_kind_name(TierKind::Typestate)    == "Tier-T (Typestate)");
static_assert(tier_kind_name(TierKind::Foundational) == "Tier-F (Foundational)");
static_assert(tier_kind_name(TierKind::Versioned)    == "Tier-V (Versioned)");

static_assert(dimension_axis_name(DimensionAxis::Type)           == "Type");
static_assert(dimension_axis_name(DimensionAxis::Refinement)     == "Refinement");
static_assert(dimension_axis_name(DimensionAxis::Usage)          == "Usage");
static_assert(dimension_axis_name(DimensionAxis::Effect)         == "Effect");
static_assert(dimension_axis_name(DimensionAxis::Security)       == "Security");
static_assert(dimension_axis_name(DimensionAxis::Protocol)       == "Protocol");
static_assert(dimension_axis_name(DimensionAxis::Lifetime)       == "Lifetime");
static_assert(dimension_axis_name(DimensionAxis::Provenance)     == "Provenance");
static_assert(dimension_axis_name(DimensionAxis::Trust)          == "Trust");
static_assert(dimension_axis_name(DimensionAxis::Representation) == "Representation");
static_assert(dimension_axis_name(DimensionAxis::Observability)  == "Observability");
static_assert(dimension_axis_name(DimensionAxis::Complexity)     == "Complexity");
static_assert(dimension_axis_name(DimensionAxis::Precision)      == "Precision");
static_assert(dimension_axis_name(DimensionAxis::Space)          == "Space");
static_assert(dimension_axis_name(DimensionAxis::Overflow)       == "Overflow");
static_assert(dimension_axis_name(DimensionAxis::Mutation)       == "Mutation");
static_assert(dimension_axis_name(DimensionAxis::Reentrancy)     == "Reentrancy");
static_assert(dimension_axis_name(DimensionAxis::Size)           == "Size");
static_assert(dimension_axis_name(DimensionAxis::Version)        == "Version");
static_assert(dimension_axis_name(DimensionAxis::Staleness)      == "Staleness");
static_assert(dimension_axis_name(DimensionAxis::Synchronization) == "Synchronization");
static_assert(dimension_axis_name(DimensionAxis::Regime)         == "Regime");
static_assert(dimension_axis_name(DimensionAxis::FpMode)         == "FpMode");
static_assert(dimension_axis_name(DimensionAxis::SyscallSurface) == "SyscallSurface");
static_assert(dimension_axis_name(DimensionAxis::ControlFlow)    == "ControlFlow");
static_assert(dimension_axis_name(DimensionAxis::CallShape)      == "CallShape");
static_assert(dimension_axis_name(DimensionAxis::StackUse)       == "StackUse");
static_assert(dimension_axis_name(DimensionAxis::GlobalState)    == "GlobalState");
static_assert(dimension_axis_name(DimensionAxis::Stdio)          == "Stdio");
static_assert(dimension_axis_name(DimensionAxis::HwInstruction)  == "HwInstruction");
static_assert(dimension_axis_name(DimensionAxis::BarrierStrength) == "BarrierStrength");
static_assert(dimension_axis_name(DimensionAxis::SimdIsa)        == "SimdIsa");

// fixy.md §24.1 axis-to-Tier mapping spot checks.
static_assert(tier_of_axis(DimensionAxis::Type)           == TierKind::Foundational);
static_assert(tier_of_axis(DimensionAxis::Refinement)     == TierKind::Foundational);
static_assert(tier_of_axis(DimensionAxis::Usage)          == TierKind::Semiring);
static_assert(tier_of_axis(DimensionAxis::Effect)         == TierKind::Semiring);
static_assert(tier_of_axis(DimensionAxis::Protocol)       == TierKind::Typestate);
static_assert(tier_of_axis(DimensionAxis::Representation) == TierKind::Lattice);
static_assert(tier_of_axis(DimensionAxis::Version)        == TierKind::Versioned);
static_assert(tier_of_axis(DimensionAxis::Staleness)      == TierKind::Semiring);
static_assert(tier_of_axis(DimensionAxis::Synchronization) == TierKind::Semiring);
static_assert(tier_of_axis(DimensionAxis::Regime)         == TierKind::Semiring);
static_assert(tier_of_axis(DimensionAxis::FpMode)         == TierKind::Semiring);
static_assert(tier_of_axis(DimensionAxis::SyscallSurface) == TierKind::Semiring);
static_assert(tier_of_axis(DimensionAxis::ControlFlow)    == TierKind::Semiring);
static_assert(tier_of_axis(DimensionAxis::CallShape)      == TierKind::Semiring);
static_assert(tier_of_axis(DimensionAxis::StackUse)       == TierKind::Semiring);
static_assert(tier_of_axis(DimensionAxis::GlobalState)    == TierKind::Semiring);
static_assert(tier_of_axis(DimensionAxis::Stdio)          == TierKind::Semiring);
static_assert(tier_of_axis(DimensionAxis::HwInstruction)  == TierKind::Semiring);
static_assert(tier_of_axis(DimensionAxis::BarrierStrength) == TierKind::Semiring);
static_assert(tier_of_axis(DimensionAxis::SimdIsa)        == TierKind::Lattice);

// Variable-template form mirrors the function form.
static_assert(tier_of_axis_v<DimensionAxis::Type>     == TierKind::Foundational);
static_assert(tier_of_axis_v<DimensionAxis::Effect>   == TierKind::Semiring);
static_assert(tier_of_axis_v<DimensionAxis::Version>  == TierKind::Versioned);

// ── Wrapper × lattice × modality × tier verification ────────────────

struct QuadTag {};
using WLinear          = Linear<int>;
using WRefined         = Refined<positive, int>;
using WSealedRefined   = SealedRefined<positive, int>;
using WTagged          = Tagged<int, source::FromUser>;
using WSecret          = Secret<int>;
using WStale           = Stale<int>;
using WTimeOrdered     = TimeOrdered<int, 4, QuadTag>;
using WMonotonic       = Monotonic<std::uint64_t>;
using WAppendOnly      = AppendOnly<int>;
using WHotPath         = HotPath<HotPathTier_v::Hot, int>;
using WDetSafe         = DetSafe<DetSafeTier_v::Pure, int>;
using WNumericalTier   = NumericalTier<Tolerance::BITEXACT, int>;
using WVendor          = Vendor<VendorBackend_v::Portable, int>;
using WResidencyHeat   = ResidencyHeat<ResidencyHeatTag_v::Hot, int>;
using WCipherTier      = CipherTier<CipherTierTag_v::Hot, int>;
using WAllocClass      = AllocClass<AllocClassTag_v::Arena, int>;
using WWait            = Wait<WaitStrategy_v::SpinPause, int>;
using WMemOrder        = MemOrder<MemOrderTag_v::SeqCst, int>;
using WProgress        = Progress<ProgressClass_v::Bounded, int>;
using WConsistency     = Consistency<Consistency_v::STRONG, int>;
using WOpaqueLifetime  = OpaqueLifetime<Lifetime_v::PER_REQUEST, int>;
using WCrash           = Crash<CrashClass_v::NoThrow, int>;
using WBudgeted        = Budgeted<int>;
using WEpochVersioned  = EpochVersioned<int>;
using WNumaPlacement   = NumaPlacement<int>;
using WRecipeSpec      = RecipeSpec<int>;
using WWitness         = Witness<Witness_v::FORMALLY_VERIFIED, int>;

static_assert(wrapper_tier_v<WLinear>         == TierKind::Semiring);
static_assert(wrapper_tier_v<WRefined>        == TierKind::Foundational);
static_assert(wrapper_tier_v<WTagged>         == TierKind::Semiring);
static_assert(wrapper_tier_v<WSecret>         == TierKind::Semiring);
static_assert(wrapper_tier_v<WTimeOrdered>    == TierKind::Lattice);
static_assert(wrapper_tier_v<WEpochVersioned> == TierKind::Versioned);

static_assert(verify_quadruple<WLinear>());
static_assert(verify_quadruple<WRefined>());
static_assert(verify_quadruple<WSealedRefined>());
static_assert(verify_quadruple<WTagged>());
static_assert(verify_quadruple<WSecret>());
static_assert(verify_quadruple<WStale>());
static_assert(verify_quadruple<WTimeOrdered>());
static_assert(verify_quadruple<WMonotonic>());
static_assert(verify_quadruple<WAppendOnly>());
static_assert(verify_quadruple<WHotPath>());
static_assert(verify_quadruple<WDetSafe>());
static_assert(verify_quadruple<WNumericalTier>());
static_assert(verify_quadruple<WVendor>());
static_assert(verify_quadruple<WResidencyHeat>());
static_assert(verify_quadruple<WCipherTier>());
static_assert(verify_quadruple<WAllocClass>());
static_assert(verify_quadruple<WWait>());
static_assert(verify_quadruple<WMemOrder>());
static_assert(verify_quadruple<WProgress>());
static_assert(verify_quadruple<WConsistency>());
static_assert(verify_quadruple<WOpaqueLifetime>());
static_assert(verify_quadruple<WCrash>());
static_assert(verify_quadruple<WBudgeted>());
static_assert(verify_quadruple<WEpochVersioned>());
static_assert(verify_quadruple<WNumaPlacement>());
static_assert(verify_quadruple<WRecipeSpec>());
static_assert(verify_quadruple<WWitness>());

// FIXY-V-054 — Witness pins Observability + Tier-S (Semiring).
static_assert(wrapper_dimension_v<WWitness> == DimensionAxis::Observability);
static_assert(wrapper_tier_v<WWitness>      == TierKind::Semiring);

}  // namespace detail::dimension_traits_self_test

}  // namespace crucible::safety
