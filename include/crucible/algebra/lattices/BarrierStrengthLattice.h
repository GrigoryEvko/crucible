#pragma once

// ── crucible::algebra::lattices::BarrierStrengthLattice ─────────────
//
// Total-order CHAIN over MEMORY-FENCE STRENGTH tiers — the grading axis
// underlying the safety/BarrierGuarded.h `BarrierGuarded<K, Arch, P>`
// wrapper (FIXY-V-255), which composes this strength axis with the
// architecture axis so a fence claim is both strength- AND arch-pinned.
//
//   None ⊑ CompilerBarrier ⊑ AcquireLoad ⊑ ReleaseStore ⊑ AcqRel
//        ⊑ SeqCst ⊑ FullFence
//
// Each tier admits EVERYTHING below it: a stronger fence satisfies a
// weaker requirement (`SeqCst::satisfies<AcqRel>` = true; the reverse is
// false).  Capability is cumulative, so the order is a genuine chain
// (a fortiori distributive).
//
// ── Why a SEPARATE axis from MemOrderLattice (FOUND-G28) ────────────
//
// `MemOrderLattice` already models the C++ `std::memory_order` TAG on an
// atomic OPERATION, and it uses the INVERTED convention (`SeqCst` at the
// bottom = strongest/most-expensive, `Relaxed` at the top = weakest).
// BarrierStrength is a DIFFERENT axis with the STANDARD convention
// (`None` at the bottom = weakest, `FullFence` at the top = strongest):
//
//   * MemOrder answers "what ordering tag did THIS atomic op carry?"
//     Its domain is exactly the five std::memory_order values.
//   * BarrierStrength answers "what fence strength does this CODE REGION
//     claim/emit?"  Its domain BRACKETS the memory_order values: below
//     them sit `None` (no barrier at all — relaxed-on-own-variable per
//     CLAUDE.md §IX) and `CompilerBarrier` (`asm volatile("":::"memory")`
//     — a compiler-only reordering barrier that is NOT a std::memory_order
//     value, e.g. bench_harness.h's DoNotOptimize fence); above them sits
//     `FullFence` (a STANDALONE architectural fence instruction —
//     x86 `mfence`, aarch64 `DMB ISH` — stronger than any single-op
//     seq_cst tag).
//
// The two axes serve different gates and are composed independently; this
// header pulls NOTHING from MemOrderLattice.  The opposite direction
// convention is intentional and called out here so a reader does not
// assume `bottom() == SeqCst` by analogy.
//
// ── On the AcquireLoad ⊑ ReleaseStore linearization ─────────────────
//
// In the strict C++ memory model, `acquire` and `release` are formally
// INCOMPARABLE: acquire orders prior loads, release orders subsequent
// visibility of prior stores; neither subsumes the other (a release
// store does NOT provide acquire semantics for a load).  This lattice
// deliberately LINEARIZES them into a STRENGTH LADDER for admission
// gating — the gate question is "does this code's declared fence provide
// AT LEAST the required ordering strength?", and Agent 11 §3.1 fixes the
// ladder order acquire ⊑ release ⊑ acq_rel.  This is sound for the gate
// because:
//   (a) the ladder is a monotone "how much ordering is guaranteed" scale,
//       NOT a formal acquire/release-provides-X proof;
//   (b) the actual platform-correctness of a fence-then-relaxed pattern
//       is asserted by an EXPLICIT grant (V-263's `barrier<Portable,
//       Release>` on the ChaseLevDeque ARM path — see below), not derived
//       from this lattice;
//   (c) unlike the SimdIsa x86/ARM trunks (V-250), where collapsing
//       incomparable elements into a chain would be UNSAFE (it would
//       admit cross-arch code), here a stronger ladder tier is always a
//       safe over-approximation of a weaker requirement.
// A future tightening could split this into a partial order with
// AcquireLoad / ReleaseStore as incomparable peers joining at AcqRel; the
// chain is the deliberate V-252 design, not an oversight.
//
// ── Tier classification (Tier-S Semiring with par=join) ─────────────
//
// BarrierStrength is `TierKind::Semiring` (the AXIS tier in
// DimensionTraits.h, shipped by V-253 — NOT a Semiring concept on the
// lattice itself).  Composition reading is "fence-strength union": two
// regions composing in parallel OR sequence admit the JOIN (the stronger
// fence) of their declared tiers — a region containing both a SeqCst site
// and a CompilerBarrier site is itself SeqCst.
//
// ── Chain order — subset-inclusion of provided ordering guarantees ──
//
//   None           = 0 — bottom; NO barrier (relaxed atomic on a thread's
//                         own variable; CLAUDE.md §IX "relaxed is OK for a
//                         thread reading its OWN atomic").  Portable
//                         everywhere; provides no cross-thread ordering.
//   CompilerBarrier = 1 — `asm volatile("" : : : "memory")`: blocks the
//                         OPTIMIZER from reordering across the point but
//                         emits NO hardware instruction.  Zero machine
//                         cost.  bench_harness.h DoNotOptimize fence.
//   AcquireLoad    = 2 — acquire ordering: prior loads cannot be reordered
//                         after this point.
//   ReleaseStore   = 3 — release ordering: subsequent visibility of prior
//                         stores is guaranteed.  (Ladder-above acquire per
//                         the deliberate linearization above.)
//   AcqRel         = 4 — combined acquire + release (RMW two-sided fence).
//   SeqCst         = 5 — sequentially-consistent: a single total order of
//                         all SeqCst operations across all threads.
//   FullFence      = 6 — top; a STANDALONE architectural fence instruction
//                         (x86 `mfence`, aarch64 `DMB ISH`) — stronger than
//                         any single-operation seq_cst tag because it
//                         fences ALL preceding/following memory ops, not
//                         just the tagged one.
//
// ── Axiom coverage ──────────────────────────────────────────────────
//
//   TypeSafe — `BarrierStrength` is a strong scoped enum (`enum class
//                : uint8_t`); cross-axis mixing requires
//                `std::to_underlying` and surfaces at the call site
//                (see test/safety_neg/neg_barrier_strength_*).
//   InitSafe — every enumerator has an explicit ordinal; reflection-
//                driven coverage fires if a switch arm is forgotten.
//   DetSafe  — lattice operations are `constexpr` (not `consteval`) so a
//                runtime Graded carrier (V-255) can enforce its
//                `pre (L::leq(...))` precondition under enforce.
//   LeakSafe — zero-state enum; no resources.
//
// ── Runtime cost ────────────────────────────────────────────────────
//
// Zero.  The enum compiles to one uint8_t; the `At<K>` singleton's
// element_type is empty and EBO-collapses to 0 bytes at every use site
// (V-255 wrapper, V-257 grants) — proven below via
// CRUCIBLE_GRADED_LAYOUT_INVARIANT.
//
// ── Forward references (deferred deliverables, by construction) ─────
//
//   FIXY-V-253 — DimensionAxis::BarrierStrength enumerator + tier_of_axis.
//   FIXY-V-255 — safety/BarrierGuarded.h: the `BarrierGuarded<K, Arch, P>`
//                wrapper composing this strength axis with the Arch axis.
//                The `row_hash_contribution<safety::BarrierGuarded<...>>`
//                federation-cache discriminator ships THERE, in
//                safety/diag/RowHashFold.h, exactly like every other
//                wrapper.  A lattice header pulls NO row_hash machinery —
//                the row_hash key is the WRAPPER, never the lattice At<>;
//                and `safety::BarrierGuarded` does not exist until V-255,
//                so the specialization is deferred by construction.
//                Mirrors VendorLattice → safety/Vendor.h and the
//                HwInstructionLattice (V-251) → safety/Hw.h (V-254)
//                deferral.
//   FIXY-V-263 — cntp/ChaseLevDeque barrier annotation: the fence-then-
//                relaxed pattern on ARM is not C++26-formally-release-
//                ordered per [atomics.fences/3.1]; the explicit
//                `barrier<Portable, Release>` grant claims platform-
//                correctness explicitly, keyed off this axis.

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/Lattice.h>
#include <crucible/algebra/lattices/ChainLattice.h>

#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>

namespace crucible::algebra::lattices {

// ── BarrierStrength — memory-fence strength taxonomy ────────────────
//
// Chain ordering: each tier strictly subsumes the ordering guarantee of
// every tier below it.  Ordinal 0 = weakest (None, no barrier); ordinal
// 6 = strongest (FullFence, standalone architectural fence).
enum class BarrierStrength : std::uint8_t {
    None            = 0,  // bottom — no barrier (relaxed-on-own-variable)
    CompilerBarrier = 1,  // asm volatile("":::"memory") — optimizer-only, no instruction
    AcquireLoad     = 2,  // acquire ordering (prior loads fenced)
    ReleaseStore    = 3,  // release ordering (prior-store visibility)
    AcqRel          = 4,  // combined acquire + release
    SeqCst          = 5,  // sequentially consistent (total order)
    FullFence       = 6,  // top — standalone mfence / DMB ISH
};

[[nodiscard]] consteval std::string_view barrier_strength_name(BarrierStrength k) noexcept {
    switch (k) {
        case BarrierStrength::None:            return "None";
        case BarrierStrength::CompilerBarrier: return "CompilerBarrier";
        case BarrierStrength::AcquireLoad:     return "AcquireLoad";
        case BarrierStrength::ReleaseStore:    return "ReleaseStore";
        case BarrierStrength::AcqRel:          return "AcqRel";
        case BarrierStrength::SeqCst:          return "SeqCst";
        case BarrierStrength::FullFence:       return "FullFence";
        default:                               return std::string_view{"<unknown BarrierStrength>"};
    }
}

struct BarrierStrengthLattice : ChainLatticeOps<BarrierStrength> {
    [[nodiscard]] static constexpr BarrierStrength bottom() noexcept {
        return BarrierStrength::None;
    }
    [[nodiscard]] static constexpr BarrierStrength top() noexcept {
        return BarrierStrength::FullFence;
    }
    [[nodiscard]] static consteval std::string_view name() noexcept {
        return "BarrierStrengthLattice";
    }

    template <BarrierStrength K>
    struct At {
        struct element_type {
            using barrier_strength_value_type = BarrierStrength;
            [[nodiscard]] constexpr operator barrier_strength_value_type() const noexcept {
                return K;
            }
            [[nodiscard]] constexpr bool operator==(element_type) const noexcept {
                return true;
            }
        };
        static constexpr BarrierStrength tier = K;
        [[nodiscard]] static constexpr element_type bottom() noexcept { return {}; }
        [[nodiscard]] static constexpr element_type top()    noexcept { return {}; }
        [[nodiscard]] static constexpr bool         leq(element_type, element_type) noexcept { return true; }
        [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static consteval std::string_view name() noexcept {
            switch (K) {
                case BarrierStrength::None:            return "BarrierStrengthLattice::At<None>";
                case BarrierStrength::CompilerBarrier: return "BarrierStrengthLattice::At<CompilerBarrier>";
                case BarrierStrength::AcquireLoad:     return "BarrierStrengthLattice::At<AcquireLoad>";
                case BarrierStrength::ReleaseStore:    return "BarrierStrengthLattice::At<ReleaseStore>";
                case BarrierStrength::AcqRel:          return "BarrierStrengthLattice::At<AcqRel>";
                case BarrierStrength::SeqCst:          return "BarrierStrengthLattice::At<SeqCst>";
                case BarrierStrength::FullFence:       return "BarrierStrengthLattice::At<FullFence>";
                default:                               return "BarrierStrengthLattice::At<?>";
            }
        }
    };
};

// ── Self-test ───────────────────────────────────────────────────────
namespace detail::barrier_strength_lattice_self_test {

// Catalog cardinality — the fence-strength chain has exactly 7 tiers.
inline constexpr std::size_t barrier_strength_count =
    std::meta::enumerators_of(^^BarrierStrength).size();

static_assert(barrier_strength_count == 7,
    "BarrierStrength diverged from {None, CompilerBarrier, AcquireLoad, "
    "ReleaseStore, AcqRel, SeqCst, FullFence}.  Adding a tier requires "
    "(a) append-only ordinal placement (FOUND-I04), (b) the matching "
    "barrier_strength_name() arm, (c) the matching At<K> name() arm, AND "
    "(d) the V-255 BarrierGuarded wrapper's row_hash + collision rules.");

// Bottom-element pin — ordinal 0 is the weakest (None, no barrier).
static_assert(std::to_underlying(BarrierStrength::None) == 0);

// Top-element pin — ordinal 6 is the strongest (FullFence).
static_assert(std::to_underlying(BarrierStrength::FullFence) == 6);

// Underlying type pin — uint8_t (mirrors HwInstruction / ControlFlow).
static_assert(std::is_same_v<std::underlying_type_t<BarrierStrength>, std::uint8_t>);

// Reflection-driven name coverage — every enumerator must resolve to a
// non-sentinel, non-empty name.  Auto-extends if the enum grows.
[[nodiscard]] consteval bool every_barrier_strength_has_name() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^BarrierStrength));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        const auto candidate = barrier_strength_name([:en:]);
        if (candidate == std::string_view{"<unknown BarrierStrength>"}) return false;
        if (candidate.empty())                                          return false;
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_barrier_strength_has_name(),
    "barrier_strength_name() switch missing an arm for at least one "
    "BarrierStrength enumerator.");

// Concept conformance — chain lattice satisfies Lattice + BoundedLattice
// and NOT Semiring (the chain order has no independent ⊕/⊗).
static_assert(::crucible::algebra::Lattice<BarrierStrengthLattice>);
static_assert(::crucible::algebra::BoundedLattice<BarrierStrengthLattice>);
static_assert(!::crucible::algebra::Semiring<BarrierStrengthLattice>);

// Exhaustive lattice-axiom verifier on (axis)³ triples.  Chain orders
// are always distributive — failure indicates a leq/join/meet defect.
static_assert(verify_chain_lattice_exhaustive<BarrierStrengthLattice>(),
    "BarrierStrengthLattice chain-order lattice axioms failed at some "
    "triple — leq/join/meet defect.");
static_assert(verify_chain_lattice_distributive_exhaustive<BarrierStrengthLattice>(),
    "BarrierStrengthLattice chain failed distributivity — leq/join/meet "
    "defect.");

// Bottom / top pins on the lattice surface (catches enum-reorder drift).
static_assert(BarrierStrengthLattice::bottom() == BarrierStrength::None);
static_assert(BarrierStrengthLattice::top()    == BarrierStrength::FullFence);

// Lattice top-level diagnostic name pin.
static_assert(BarrierStrengthLattice::name() == std::string_view{"BarrierStrengthLattice"});

// Strict-chain order pin (bottom ⊏ top witness).
static_assert( BarrierStrengthLattice::leq(BarrierStrength::None, BarrierStrength::FullFence));
static_assert(!BarrierStrengthLattice::leq(BarrierStrength::FullFence, BarrierStrength::None));

// Mid-chain ordering — every tier strictly subsumes the previous.
static_assert(BarrierStrengthLattice::leq(BarrierStrength::None,            BarrierStrength::CompilerBarrier));
static_assert(BarrierStrengthLattice::leq(BarrierStrength::CompilerBarrier, BarrierStrength::AcquireLoad));
static_assert(BarrierStrengthLattice::leq(BarrierStrength::AcquireLoad,     BarrierStrength::ReleaseStore));
static_assert(BarrierStrengthLattice::leq(BarrierStrength::ReleaseStore,    BarrierStrength::AcqRel));
static_assert(BarrierStrengthLattice::leq(BarrierStrength::AcqRel,          BarrierStrength::SeqCst));
static_assert(BarrierStrengthLattice::leq(BarrierStrength::SeqCst,          BarrierStrength::FullFence));

// The load-bearing `satisfies` witness from the task spec: a stronger
// fence satisfies a weaker requirement, the reverse does not.
static_assert(BarrierStrengthLattice::leq(BarrierStrength::AcqRel, BarrierStrength::SeqCst),
    "FIXY-V-252: SeqCst::satisfies<AcqRel> — a SeqCst fence satisfies an "
    "AcqRel requirement (leq(AcqRel, SeqCst) is the satisfies direction).");
static_assert(!BarrierStrengthLattice::leq(BarrierStrength::SeqCst, BarrierStrength::AcqRel),
    "FIXY-V-252: AcqRel does NOT satisfy a SeqCst requirement.");

// Reverse direction must fail for non-equal pairs.
static_assert(!BarrierStrengthLattice::leq(BarrierStrength::CompilerBarrier, BarrierStrength::None));
static_assert(!BarrierStrengthLattice::leq(BarrierStrength::FullFence,       BarrierStrength::SeqCst));

// Join semantics — par=join (stronger-fence-dominates).  Composing a
// CompilerBarrier site with a SeqCst site yields SeqCst.
static_assert(BarrierStrengthLattice::join(BarrierStrength::CompilerBarrier, BarrierStrength::SeqCst)
              == BarrierStrength::SeqCst);
// None is the join identity (composing with a no-barrier site never
// strengthens the claim).
static_assert(BarrierStrengthLattice::join(BarrierStrength::None, BarrierStrength::AcquireLoad)
              == BarrierStrength::AcquireLoad);
// FullFence absorbs in join.
static_assert(BarrierStrengthLattice::join(BarrierStrength::FullFence, BarrierStrength::AcqRel)
              == BarrierStrength::FullFence);

// Meet semantics — and=meet (weaker-fence-floor).  At an admission gate,
// meeting a strong binding with a weak policy yields the weak floor.
static_assert(BarrierStrengthLattice::meet(BarrierStrength::FullFence, BarrierStrength::AcquireLoad)
              == BarrierStrength::AcquireLoad);
static_assert(BarrierStrengthLattice::meet(BarrierStrength::None, BarrierStrength::SeqCst)
              == BarrierStrength::None);

// ── FIXY-FOUND-076 audit pin: cross-tree convention alignment ────────
//
// FOUND-009 and FOUND-010 discovered that MemOrderLattice and
// HwInstructionLattice's join direction is INVERTED relative to the
// cross-tree "par=join, strictest-wins" contract documented in
// DimensionTraits.h L275-L335 — both linearize their chains with
// strictest=BOTTOM, so MEET (chain-min) returns strictest, JOIN returns
// weakest.
//
// FOUND-076 sweeps the remaining Tier-S lattices to determine which
// match the cross-tree contract and which carry the inverted convention
// (and need the same explanatory pin block FOUND-009/010 added).
//
// AUDIT RESULT for BarrierStrengthLattice (2026-05-25): ALIGNED.
//   * chain direction: None (bottom) → CompilerBarrier → AcquireLoad
//     → ReleaseStore → AcqRel → SeqCst → FullFence (top)
//   * join(weaker, stronger) returns stronger (FullFence absorbs)
//   * meet(weaker, stronger) returns weaker (None absorbs)
//   * cross-tree reading: "par=join, strictest-wins" ✓
//
// Consumers wanting strictest-wins composition can call JOIN directly
// on this lattice — NO inverted-convention warning needed, unlike
// MemOrder/HwInstruction.
//
// Polarity-witness pin: a refactor flipping the chain direction (so
// strictest moves to bottom) would red THIS assert in lockstep with
// the FOUND-009/010 convention.  Pinning both directions makes the
// audit one-grep-discoverable.
static_assert(BarrierStrengthLattice::join(BarrierStrength::None,
                                           BarrierStrength::FullFence)
              == BarrierStrength::FullFence,
    "FIXY-FOUND-076: BarrierStrengthLattice's JOIN gives strictest-wins "
    "under the natural-strength-lattice convention (top=FullFence). "
    "join(None, FullFence) returns FullFence — the stronger of the two. "
    "This matches the cross-tree 'par=join, strictest-wins' contract; "
    "consumers can call JOIN directly here, UNLIKE MemOrder/HwInstruction "
    "which require MEET for strictest-wins (FOUND-009/010).");
static_assert(BarrierStrengthLattice::meet(BarrierStrength::None,
                                           BarrierStrength::FullFence)
              == BarrierStrength::None,
    "FIXY-FOUND-076: BarrierStrengthLattice's MEET gives weakest-floor "
    "(bottom=None).  CSL/admission gates wanting capability-minimization "
    "MUST call MEET — None absorbs in meet by chain-minimum semantics.");

// At<K> singleton — empty element_type for EBO collapse at every use
// site.  V-255's `Graded<Absolute, At<K>, P>` relies on this.
static_assert(std::is_empty_v<BarrierStrengthLattice::At<BarrierStrength::None>::element_type>);
static_assert(std::is_empty_v<BarrierStrengthLattice::At<BarrierStrength::CompilerBarrier>::element_type>);
static_assert(std::is_empty_v<BarrierStrengthLattice::At<BarrierStrength::AcqRel>::element_type>);
static_assert(std::is_empty_v<BarrierStrengthLattice::At<BarrierStrength::FullFence>::element_type>);

// At<K>::tier pins the enum value at the type level — what V-255+
// wrappers key on for compile-time admission decisions.
static_assert(BarrierStrengthLattice::At<BarrierStrength::SeqCst>::tier == BarrierStrength::SeqCst);

// At<K>::name() coverage — reflection-driven, mirrors the enum-name probe.
[[nodiscard]] consteval bool every_at_barrier_strength_has_name() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^BarrierStrength));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (BarrierStrengthLattice::At<([:en:])>::name() ==
            std::string_view{"BarrierStrengthLattice::At<?>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_at_barrier_strength_has_name(),
    "BarrierStrengthLattice::At<K>::name() switch missing an arm.");

// ── Layout invariants — Graded<Absolute, At<K>, P> == sizeof(P) ─────
struct OneByteValue   { char c{0}; };
struct EightByteValue { unsigned long long v{0}; };

template <typename T_>
using NoneGraded = Graded<ModalityKind::Absolute,
                          BarrierStrengthLattice::At<BarrierStrength::None>, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(NoneGraded, OneByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(NoneGraded, EightByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(NoneGraded, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(NoneGraded, double);

template <typename T_>
using FullFenceGraded = Graded<ModalityKind::Absolute,
                               BarrierStrengthLattice::At<BarrierStrength::FullFence>, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(FullFenceGraded, EightByteValue);

// Runtime smoke test — per feedback_algebra_runtime_smoke_test_discipline:
// pure static_asserts can mask consteval/SFINAE/inline-body bugs.
inline void barrier_strength_lattice_runtime_smoke_test() {
    BarrierStrength a = BarrierStrength::None;
    BarrierStrength b = BarrierStrength::FullFence;
    [[maybe_unused]] bool            rl = BarrierStrengthLattice::leq(a, b);
    [[maybe_unused]] BarrierStrength rj = BarrierStrengthLattice::join(a, b);
    [[maybe_unused]] BarrierStrength rm = BarrierStrengthLattice::meet(a, b);
    [[maybe_unused]] BarrierStrength bot = BarrierStrengthLattice::bottom();
    [[maybe_unused]] BarrierStrength topv = BarrierStrengthLattice::top();

    // Mid-chain witnesses — the satisfies ladder.
    BarrierStrength acqrel = BarrierStrength::AcqRel;
    BarrierStrength seqcst = BarrierStrength::SeqCst;
    [[maybe_unused]] bool seqcst_satisfies_acqrel = BarrierStrengthLattice::leq(acqrel, seqcst);
    [[maybe_unused]] BarrierStrength rj2 = BarrierStrengthLattice::join(acqrel, seqcst);
    [[maybe_unused]] BarrierStrength rm2 = BarrierStrengthLattice::meet(acqrel, seqcst);

    // At<K>::element_type round-trip — verify the singleton's conversion
    // materializes the right tier at runtime, not just at consteval.
    BarrierStrengthLattice::At<BarrierStrength::ReleaseStore>::element_type rel_pin{};
    [[maybe_unused]] BarrierStrength rel_recovered = rel_pin;

    // Graded carrier round-trip on the regime-1 EBO shape.
    OneByteValue payload{9};
    NoneGraded<OneByteValue> initial{
        payload, BarrierStrengthLattice::At<BarrierStrength::None>::bottom()};
    auto widened  = initial.weaken(BarrierStrengthLattice::At<BarrierStrength::None>::top());
    auto composed = initial.compose(widened);
    [[maybe_unused]] auto grade  = widened.grade();
    [[maybe_unused]] auto peeked = composed.peek().c;
}

}  // namespace detail::barrier_strength_lattice_self_test

}  // namespace crucible::algebra::lattices
