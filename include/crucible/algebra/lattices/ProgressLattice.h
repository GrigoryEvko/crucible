#pragma once

// ── crucible::algebra::lattices::ProgressLattice ────────────────────
//
// Four-tier total-order chain lattice over the termination-guarantee
// spectrum.  The grading axis underlying the Progress wrapper from
// 28_04_2026_effects.md §4.3.5 — FIFTH and FINAL Month-2 chain
// lattice, closing the first-pass catalog (DetSafe + HotPath + Wait
// + MemOrder + Progress).
//
// Captures the `term` / `nterm` aspects of the session-type stack's
// φ-family (per session_types.md III.L7 and Task #346 SessionSafety.h
// — currently unshipped).  Composes orthogonally with the four
// sister chain lattices via wrapper-nesting per 28_04 §4.7.
//
// ── The classification ──────────────────────────────────────────────
//
// Each tier names a CLASS of termination promise a function makes
// about its execution.  A function declared at tier T promises
// termination behavior at level T (or stronger — i.e., higher in the
// chain).
//
//     MayDiverge   — No termination guarantee.  Function might run
//                    forever (e.g., REPL loops, event handlers,
//                    server main loops, Inferlet user code with
//                    unbounded recursion).  The escape hatch — the
//                    weakest claim possible.  Production: Inferlet
//                    user-supplied PDA decode loops (per Pie SOSP
//                    2025 inferlet pattern).
//     Terminating  — Function eventually halts.  No bound on how
//                    long it takes, but it does eventually return
//                    or throw.  Production: Lean proof tactics,
//                    one-shot search functions, bounded-but-
//                    unspecified loops.
//     Productive   — Function not only terminates but makes
//                    observable progress at every step / iteration
//                    (no silent stalls).  Eventually-fair scheduler
//                    primitives, pipelined operators that emit at
//                    least one element per N iterations.
//                    Production: BackgroundThread::drain (every
//                    iteration drains at least one entry or signals
//                    quiescence).
//     Bounded      — Function halts within a HARD wall-clock budget
//                    declared at the type level.  The strongest
//                    termination claim — admissible in deadline-
//                    sensitive call sites.  Production: Forge
//                    phases per FORGE.md §5 (hard wall-clock budget
//                    per phase); CNTP heartbeat handlers.
//
// ── Algebraic shape ─────────────────────────────────────────────────
//
// Carrier: enum class ProgressClass ∈ the four tiers above.
// Order:   MayDiverge ⊑ Terminating ⊑ Productive ⊑ Bounded.
//
// Bottom = MayDiverge (weakest claim; satisfies only MayDiverge-
//                      tolerating consumers, i.e., escape-hatch
//                      contexts).
// Top    = Bounded    (strongest claim; satisfies every consumer —
//                      a wall-clock-bounded function is admissible
//                      everywhere).
// Join   = max        (the more-restricted of two providers).
// Meet   = min        (the less-restricted of two providers).
//
// ── Direction convention ────────────────────────────────────────────
//
// Stronger guarantee = HIGHER in the lattice.  `leq(weak, strong)`
// reads "a weaker-budget consumer is satisfied by a stronger-budget
// provider" — a Bounded-tier function can be invoked from any
// consumer site because its hard wall-clock bound is the strongest
// termination promise possible.
//
// This is the Crucible-standard subsumption-up direction, shared
// with the four sister chain lattices (Tolerance / Consistency /
// Lifetime / DetSafe / HotPath / Wait / MemOrder).
//
// ── HAPPY ALIGNMENT WITH 28_04_2026_effects.md §4.3.5 SPEC ─────────
//
// Unlike the previous four Month-2 chain lattices (DetSafe / HotPath
// / Wait / MemOrder) which all required inverting the spec's enum
// ordinal to preserve the project bottom=weakest convention, the
// Progress spec's ordinal NATURALLY ALIGNS with project convention:
//
//   spec:    MayDiverge=0, Terminating=1, Productive=2, Bounded=3
//   project: bottom=0=weakest, top=N-1=strongest
//
// MayDiverge IS the weakest (most permissive admission), Bounded IS
// the strongest (most restrictive admission).  No inversion needed.
// This is a happy coincidence stemming from the spec author choosing
// the SEMANTICALLY-RIGHT ordinal direction for this particular
// lattice (rather than mirroring an external standard's enum order
// like std::memory_order or memory-cost ordering).
//
//   Axiom coverage:
//     TypeSafe — ProgressClass is a strong scoped enum (`enum class
//                : uint8_t`); cross-class mixing requires
//                `std::to_underlying` and is surfaced at the call
//                site.
//     ThreadSafe — Productive/Bounded contracts compose with
//                  CLAUDE.md §IX cache-tier rules: a function
//                  Bounded by wall clock can be admitted on the
//                  hot path even if its working set spans tiers,
//                  because the wall-clock bound is the load-bearing
//                  constraint.
//   Runtime cost:
//     leq / join / meet — single integer compare and a select; the
//     four-element domain compiles to a 1-byte field with a single
//     branch.  When wrapped at a fixed type-level class via
//     `ProgressLattice::At<ProgressClass::Bounded>` (the conf::Tier
//     pattern), the grade EBO-collapses to zero bytes.
//
// ── At<T> singleton sub-lattice ─────────────────────────────────────
//
// Mirrors the four sister chain lattices: a per-ProgressClass
// singleton sub-lattice with empty element_type.
// `Graded<Absolute, ProgressLattice::At<ProgressClass::Bounded>, T>`
// pays zero runtime overhead for the grade itself.
//
// See FOUND-G33 (this file) for the lattice; FOUND-G34
// (safety/Progress.h) for the type-pinned wrapper;
// 28_04_2026_effects.md §4.3.5 for the production-call-site
// rationale; session_types.md III.L7 for the φ-family connection;
// FORGE.md §5 for the Bounded-class production usage in Forge phases.

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/Lattice.h>
#include <crucible/algebra/lattices/ChainLattice.h>

#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::algebra::lattices {

// ── ProgressClass — chain over the termination-guarantee spectrum ──
//
// Ordinal convention: MayDiverge=0 (bottom) ... Bounded=3 (top).
// HAPPILY ALIGNS with both the project convention (bottom=0=weakest)
// AND the 28_04 spec's ordinal hint — see lattice docblock above
// for the alignment rationale.
enum class ProgressClass : std::uint8_t {
    MayDiverge  = 0,    // bottom: no termination guarantee
    Terminating = 1,    // halts eventually (no bound)
    Productive  = 2,    // halts AND makes observable progress every step
    Bounded     = 3,    // top: halts within hard wall-clock budget
};

// Cardinality + diagnostic name via reflection.
inline constexpr std::size_t progress_class_count =
    std::meta::enumerators_of(^^ProgressClass).size();

[[nodiscard]] consteval std::string_view progress_class_name(ProgressClass c) noexcept {
    switch (c) {
        case ProgressClass::MayDiverge:  return "MayDiverge";
        case ProgressClass::Terminating: return "Terminating";
        case ProgressClass::Productive:  return "Productive";
        case ProgressClass::Bounded:     return "Bounded";
        default:                         return std::string_view{"<unknown ProgressClass>"};
    }
}

// ── Full ProgressLattice (chain order) ──────────────────────────────
struct ProgressLattice : ChainLatticeOps<ProgressClass> {
    [[nodiscard]] static constexpr element_type bottom() noexcept {
        return ProgressClass::MayDiverge;
    }
    [[nodiscard]] static constexpr element_type top() noexcept {
        return ProgressClass::Bounded;
    }

    [[nodiscard]] static consteval std::string_view name() noexcept {
        return "ProgressLattice";
    }

    // ── At<T>: singleton sub-lattice at a fixed type-level class ────
    template <ProgressClass T>
    struct At {
        struct element_type {
            using progress_class_value_type = ProgressClass;
            [[nodiscard]] constexpr operator progress_class_value_type() const noexcept {
                return T;
            }
            [[nodiscard]] constexpr bool operator==(element_type) const noexcept {
                return true;
            }
        };

        static constexpr ProgressClass cls = T;

        [[nodiscard]] static constexpr element_type bottom() noexcept { return {}; }
        [[nodiscard]] static constexpr element_type top()    noexcept { return {}; }
        [[nodiscard]] static constexpr bool         leq(element_type, element_type) noexcept { return true; }
        [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }

        [[nodiscard]] static consteval std::string_view name() noexcept {
            switch (T) {
                case ProgressClass::MayDiverge:  return "ProgressLattice::At<MayDiverge>";
                case ProgressClass::Terminating: return "ProgressLattice::At<Terminating>";
                case ProgressClass::Productive:  return "ProgressLattice::At<Productive>";
                case ProgressClass::Bounded:     return "ProgressLattice::At<Bounded>";
                default:                         return "ProgressLattice::At<?>";
            }
        }
    };
};

// ── Convenience aliases ─────────────────────────────────────────────
namespace progress_class {
    using MayDivergeClass  = ProgressLattice::At<ProgressClass::MayDiverge>;
    using TerminatingClass = ProgressLattice::At<ProgressClass::Terminating>;
    using ProductiveClass  = ProgressLattice::At<ProgressClass::Productive>;
    using BoundedClass     = ProgressLattice::At<ProgressClass::Bounded>;
}  // namespace progress_class

// ── Self-test ───────────────────────────────────────────────────────
namespace detail::progress_lattice_self_test {

// Cardinality + reflection-based name coverage.
static_assert(progress_class_count == 4,
    "ProgressClass catalog diverged from {MayDiverge, Terminating, "
    "Productive, Bounded}; confirm intent and update the dispatcher's "
    "termination-class admission gates.");

[[nodiscard]] consteval bool every_progress_class_has_name() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^ProgressClass));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (progress_class_name([:en:]) ==
            std::string_view{"<unknown ProgressClass>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_progress_class_has_name(),
    "progress_class_name() switch missing arm for at least one class.");

// Concept conformance.
static_assert(Lattice<ProgressLattice>);
static_assert(BoundedLattice<ProgressLattice>);
static_assert(Lattice<progress_class::MayDivergeClass>);
static_assert(Lattice<progress_class::TerminatingClass>);
static_assert(Lattice<progress_class::ProductiveClass>);
static_assert(Lattice<progress_class::BoundedClass>);
static_assert(BoundedLattice<progress_class::BoundedClass>);

static_assert(!UnboundedLattice<ProgressLattice>);
static_assert(!Semiring<ProgressLattice>);

// Empty element_type for EBO collapse.
static_assert(std::is_empty_v<progress_class::BoundedClass::element_type>);
static_assert(std::is_empty_v<progress_class::ProductiveClass::element_type>);
static_assert(std::is_empty_v<progress_class::TerminatingClass::element_type>);
static_assert(std::is_empty_v<progress_class::MayDivergeClass::element_type>);

// EXHAUSTIVE lattice-axiom + distributivity coverage over
// (ProgressClass)³ = 64 triples each.
static_assert(verify_chain_lattice_exhaustive<ProgressLattice>(),
    "ProgressLattice's chain-order lattice axioms must hold at every "
    "(ProgressClass)³ triple — failure indicates a defect in "
    "leq/join/meet.");
static_assert(verify_chain_lattice_distributive_exhaustive<ProgressLattice>(),
    "ProgressLattice's chain order must satisfy distributivity at "
    "every (ProgressClass)³ triple.");

// Direct order witnesses — the entire chain is increasing, with
// Bounded at the top (strongest termination guarantee) and
// MayDiverge at the bottom (escape hatch).
static_assert( ProgressLattice::leq(ProgressClass::MayDiverge,  ProgressClass::Terminating));
static_assert( ProgressLattice::leq(ProgressClass::Terminating, ProgressClass::Productive));
static_assert( ProgressLattice::leq(ProgressClass::Productive,  ProgressClass::Bounded));
static_assert( ProgressLattice::leq(ProgressClass::MayDiverge,  ProgressClass::Bounded));   // transitive
static_assert(!ProgressLattice::leq(ProgressClass::Bounded,     ProgressClass::MayDiverge));
static_assert(!ProgressLattice::leq(ProgressClass::Productive,  ProgressClass::Terminating));
static_assert(!ProgressLattice::leq(ProgressClass::Bounded,     ProgressClass::Productive));

// Pin bottom / top to chain endpoints.
static_assert(ProgressLattice::bottom() == ProgressClass::MayDiverge);
static_assert(ProgressLattice::top()    == ProgressClass::Bounded);

// Join strengthens (max); meet weakens (min).
static_assert(ProgressLattice::join(ProgressClass::MayDiverge, ProgressClass::Bounded)
              == ProgressClass::Bounded);
static_assert(ProgressLattice::join(ProgressClass::Terminating, ProgressClass::Productive)
              == ProgressClass::Productive);
static_assert(ProgressLattice::meet(ProgressClass::MayDiverge, ProgressClass::Bounded)
              == ProgressClass::MayDiverge);
static_assert(ProgressLattice::meet(ProgressClass::Productive, ProgressClass::Bounded)
              == ProgressClass::Productive);

// Diagnostic names.
static_assert(ProgressLattice::name() == "ProgressLattice");
static_assert(progress_class::MayDivergeClass::name()  == "ProgressLattice::At<MayDiverge>");
static_assert(progress_class::TerminatingClass::name() == "ProgressLattice::At<Terminating>");
static_assert(progress_class::ProductiveClass::name()  == "ProgressLattice::At<Productive>");
static_assert(progress_class::BoundedClass::name()     == "ProgressLattice::At<Bounded>");

// Reflection-driven coverage check on At<T>::name().
[[nodiscard]] consteval bool every_at_progress_class_has_name() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^ProgressClass));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (ProgressLattice::At<([:en:])>::name() ==
            std::string_view{"ProgressLattice::At<?>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_at_progress_class_has_name(),
    "ProgressLattice::At<T>::name() switch missing an arm for at "
    "least one class.");

// Convenience aliases resolve correctly.
static_assert(progress_class::MayDivergeClass::cls  == ProgressClass::MayDiverge);
static_assert(progress_class::TerminatingClass::cls == ProgressClass::Terminating);
static_assert(progress_class::ProductiveClass::cls  == ProgressClass::Productive);
static_assert(progress_class::BoundedClass::cls     == ProgressClass::Bounded);

// ── Layout invariants on Graded<...,At<T>,T_> ───────────────────────
struct OneByteValue   { char c{0}; };
struct EightByteValue { unsigned long long v{0}; };

// BoundedClass — most semantically-loaded (Forge phase wall-clock
// guarantees + CNTP heartbeat handlers).
template <typename T_>
using BoundedGraded = Graded<ModalityKind::Absolute, progress_class::BoundedClass, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(BoundedGraded, OneByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(BoundedGraded, EightByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(BoundedGraded, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(BoundedGraded, double);

// ProductiveClass — bg-thread drain loops.
template <typename T_>
using ProductiveGraded = Graded<ModalityKind::Absolute, progress_class::ProductiveClass, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(ProductiveGraded, EightByteValue);

// MayDivergeClass — escape hatch tier (Inferlet user code).
template <typename T_>
using MayDivergeGraded = Graded<ModalityKind::Absolute, progress_class::MayDivergeClass, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(MayDivergeGraded, EightByteValue);

// ── Runtime smoke test ─────────────────────────────────────────────
inline void runtime_smoke_test() {
    ProgressClass a = ProgressClass::MayDiverge;
    ProgressClass b = ProgressClass::Bounded;
    [[maybe_unused]] bool          l1   = ProgressLattice::leq(a, b);
    [[maybe_unused]] ProgressClass j1   = ProgressLattice::join(a, b);
    [[maybe_unused]] ProgressClass m1   = ProgressLattice::meet(a, b);
    [[maybe_unused]] ProgressClass bot  = ProgressLattice::bottom();
    [[maybe_unused]] ProgressClass topv = ProgressLattice::top();

    // Mid-tier ops — chain through Terminating / Productive boundary.
    ProgressClass term = ProgressClass::Terminating;
    ProgressClass prod = ProgressClass::Productive;
    [[maybe_unused]] ProgressClass j2 = ProgressLattice::join(term, prod);   // Productive
    [[maybe_unused]] ProgressClass m2 = ProgressLattice::meet(term, prod);   // Terminating

    // Graded<Absolute, BoundedClass, T> at runtime.
    OneByteValue v{42};
    BoundedGraded<OneByteValue> initial{v, progress_class::BoundedClass::bottom()};
    auto widened   = initial.weaken(progress_class::BoundedClass::top());
    auto composed  = initial.compose(widened);
    auto rv_widen  = std::move(widened).weaken(progress_class::BoundedClass::top());

    [[maybe_unused]] auto g  = rv_widen.grade();
    [[maybe_unused]] auto vc = composed.peek().c;

    // Conversion: At<ProgressClass>::element_type → ProgressClass.
    progress_class::BoundedClass::element_type e{};
    [[maybe_unused]] ProgressClass rec = e;
}

}  // namespace detail::progress_lattice_self_test

}  // namespace crucible::algebra::lattices
