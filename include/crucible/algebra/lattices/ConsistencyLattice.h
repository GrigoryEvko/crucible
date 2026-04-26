#pragma once

// ── crucible::algebra::lattices::ConsistencyLattice ─────────────────
//
// Five-element total-order lattice over distributed-system consistency
// guarantees.  The grading axis underlying §5 BatchPolicy<Axis, Level>
// from 25_04_2026.md (the Peepco-style per-axis consistency selector
// that lets TP run synchronously over NVLink while DP runs Decoupled-
// DiLoCo with bounded staleness across regions, in the same training
// run, with the same DAG, under the same NumericalRecipe).
//
// ── The classification ──────────────────────────────────────────────
//
//     EVENTUAL          — the weakest practical guarantee.  All
//                          replicas eventually converge in the
//                          absence of new updates.  Cheapest to
//                          provide; tolerates arbitrary reordering
//                          and arbitrary delay.
//     READ_YOUR_WRITES  — a session observes its own writes
//                          immediately; sees others' writes
//                          eventually.  Anti-staleness only for the
//                          local writer.
//     CAUSAL_PREFIX     — observed writes form a prefix of some
//                          causal-order linearization.  No
//                          divergence on causally-related writes;
//                          concurrent writes may interleave
//                          arbitrarily.  The CRDT-canonical
//                          guarantee for collaborative state.
//     BOUNDED_STALENESS — observers see writes at most K outer-steps
//                          old.  Strictly stronger than causal-
//                          prefix because the staleness bound is
//                          REAL-TIME, not just causal.
//     STRONG            — linearizable: every observer sees the
//                          same total order on writes, and that
//                          order extends real-time.  The strictest
//                          guarantee; required for intra-tensor
//                          partial sums (TP) where reduction order
//                          must rejoin within a step.
//
// ── Algebraic shape ─────────────────────────────────────────────────
//
// Carrier: enum class Consistency over the five tiers above.
// Order:   EVENTUAL ⊑ READ_YOUR_WRITES ⊑ CAUSAL_PREFIX
//                  ⊑ BOUNDED_STALENESS ⊑ STRONG.
//
// Bottom = EVENTUAL (the weakest — easiest to provide; satisfies the
//                    fewest workloads).
// Top    = STRONG   (the strictest — hardest to provide; subsumes
//                    every weaker tier).
// Join   = max      (what satisfies BOTH demands — the stricter of
//                    two requirements).
// Meet   = min      (what's satisfied by either provider — the
//                    weaker of two providers).
//
// ── Direction convention (matches ConfLattice / LifetimeLattice) ────
//
// Stronger guarantee = higher in the lattice.  `leq(weak, strong) =
// true` reads "the weaker requirement is subsumed by the stronger
// guarantee" — a Strong-consistency provider satisfies any consumer
// asking for Eventual, ReadYourWrites, etc.
//
// Per §5 the BatchPolicy<Axis, Level> picks PER AXIS the WEAKEST
// consistency that the workload tolerates — TP commits to STRONG
// because intra-tensor partial sums must rejoin within a step; DP
// can drop to EVENTUAL because gradient aggregation has well-known
// convergence guarantees under bounded delay.  The lattice gives
// the algebra; the per-axis selector lives in BatchPolicy itself
// (#460+ scope, not this lattice's responsibility).
//
//   Axiom coverage:
//     TypeSafe — Consistency is a strong enum (`enum class : uint8_t`);
//                conversion to the underlying type requires
//                `std::to_underlying`, blocking accidental int math
//                on classification levels.
//     DetSafe — every operation is `constexpr` (NOT `consteval`) so
//                Graded's runtime `pre (L::leq(...))` precondition can
//                fire under the `enforce` contract semantic.
//   Runtime cost:
//     leq / join / meet — single integer compare and a select; the
//     five-element domain compiles to a 1-byte field with a single
//     branch.  When wrapped at a fixed type-level tier via
//     `ConsistencyLattice::At<Consistency::STRONG>` (the conf::Tier
//     pattern), the grade EBO-collapses to zero bytes.
//
// ── At<C> singleton sub-lattice ─────────────────────────────────────
//
// Mirrors ConfLattice::At<Conf>: a per-Consistency singleton
// sub-lattice with empty element_type, used when an axis's
// consistency tier is fixed at the type level.  `Graded<Absolute,
// ConsistencyLattice::At<Consistency::STRONG>, T>` pays zero runtime
// overhead for the grade itself — the entire classification is
// encoded in the type parameter.
//
// See ALGEBRA-14 (#459), ALGEBRA-2 (Lattice.h) for the verifier
// helpers; ALGEBRA-6 (ConfLattice) for the convention this lattice
// mirrors; 25_04_2026.md §5 for the BatchPolicy use case.

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/Lattice.h>
#include <crucible/algebra/lattices/ChainLattice.h>

#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::algebra::lattices {

// ── Consistency tier ────────────────────────────────────────────────
enum class Consistency : std::uint8_t {
    EVENTUAL          = 0,    // weakest — eventually converges
    READ_YOUR_WRITES  = 1,    // local observer sees own writes immediately
    CAUSAL_PREFIX     = 2,    // observed prefix of causal linearization
    BOUNDED_STALENESS = 3,    // real-time staleness bound (K outer-steps)
    STRONG            = 4,    // linearizable; strictest
};

// Cardinality + diagnostic name via reflection.
inline constexpr std::size_t consistency_count =
    std::meta::enumerators_of(^^Consistency).size();

[[nodiscard]] consteval std::string_view consistency_name(Consistency c) noexcept {
    switch (c) {
        case Consistency::EVENTUAL:          return "EVENTUAL";
        case Consistency::READ_YOUR_WRITES:  return "READ_YOUR_WRITES";
        case Consistency::CAUSAL_PREFIX:     return "CAUSAL_PREFIX";
        case Consistency::BOUNDED_STALENESS: return "BOUNDED_STALENESS";
        case Consistency::STRONG:            return "STRONG";
        default:                             return std::string_view{"<unknown Consistency>"};
    }
}

// ── Full ConsistencyLattice (chain order) ───────────────────────────
//
// Inherits leq/join/meet from ChainLatticeOps<Consistency> — see
// ChainLattice.h for the rationale (audit Tier-2 dedup).
struct ConsistencyLattice : ChainLatticeOps<Consistency> {
    [[nodiscard]] static constexpr element_type bottom() noexcept {
        return Consistency::EVENTUAL;
    }
    [[nodiscard]] static constexpr element_type top() noexcept {
        return Consistency::STRONG;
    }

    [[nodiscard]] static consteval std::string_view name() noexcept {
        return "ConsistencyLattice";
    }

    // ── At<C>: singleton sub-lattice at a fixed type-level tier ─────
    //
    // Used by per-axis consistency wrappers in BatchPolicy: e.g.
    //   using TpAxis = Graded<Absolute, ConsistencyLattice::At<STRONG>, ...>;
    template <Consistency C>
    struct At {
        struct element_type {
            using consistency_value_type = Consistency;
            [[nodiscard]] constexpr operator consistency_value_type() const noexcept {
                return C;
            }
            [[nodiscard]] constexpr bool operator==(element_type) const noexcept {
                return true;
            }
        };

        static constexpr Consistency tier = C;

        [[nodiscard]] static constexpr element_type bottom() noexcept { return {}; }
        [[nodiscard]] static constexpr element_type top()    noexcept { return {}; }
        [[nodiscard]] static constexpr bool         leq(element_type, element_type) noexcept { return true; }
        [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }

        [[nodiscard]] static consteval std::string_view name() noexcept {
            switch (C) {
                case Consistency::EVENTUAL:          return "ConsistencyLattice::At<EVENTUAL>";
                case Consistency::READ_YOUR_WRITES:  return "ConsistencyLattice::At<READ_YOUR_WRITES>";
                case Consistency::CAUSAL_PREFIX:     return "ConsistencyLattice::At<CAUSAL_PREFIX>";
                case Consistency::BOUNDED_STALENESS: return "ConsistencyLattice::At<BOUNDED_STALENESS>";
                case Consistency::STRONG:            return "ConsistencyLattice::At<STRONG>";
                default:                             return "ConsistencyLattice::At<?>";
            }
        }
    };
};

// ── Convenience aliases ─────────────────────────────────────────────
namespace consistency {
    using EventualTier         = ConsistencyLattice::At<Consistency::EVENTUAL>;
    using ReadYourWritesTier   = ConsistencyLattice::At<Consistency::READ_YOUR_WRITES>;
    using CausalPrefixTier     = ConsistencyLattice::At<Consistency::CAUSAL_PREFIX>;
    using BoundedStalenessTier = ConsistencyLattice::At<Consistency::BOUNDED_STALENESS>;
    using StrongTier           = ConsistencyLattice::At<Consistency::STRONG>;
}  // namespace consistency

// ── Self-test ───────────────────────────────────────────────────────
namespace detail::consistency_lattice_self_test {

// Cardinality + reflection-based name coverage.
static_assert(consistency_count == 5,
    "Consistency catalog diverged from {EVENTUAL, READ_YOUR_WRITES, "
    "CAUSAL_PREFIX, BOUNDED_STALENESS, STRONG}; confirm intent and "
    "update BatchPolicy callers.");

[[nodiscard]] consteval bool every_consistency_has_name() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^Consistency));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (consistency_name([:en:]) ==
            std::string_view{"<unknown Consistency>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_consistency_has_name(),
    "consistency_name() switch missing arm for at least one tier — "
    "add the arm or the new tier leaks the '<unknown Consistency>' "
    "sentinel into Augur's debug output.");

// Concept conformance — full lattice + each At<C> sub-lattice.
static_assert(Lattice<ConsistencyLattice>);
static_assert(BoundedLattice<ConsistencyLattice>);
static_assert(Lattice<consistency::EventualTier>);
static_assert(Lattice<consistency::ReadYourWritesTier>);
static_assert(Lattice<consistency::CausalPrefixTier>);
static_assert(Lattice<consistency::BoundedStalenessTier>);
static_assert(Lattice<consistency::StrongTier>);
static_assert(BoundedLattice<consistency::StrongTier>);

// Negative concept assertions — pin ConsistencyLattice's character.
static_assert(!UnboundedLattice<ConsistencyLattice>);
static_assert(!Semiring<ConsistencyLattice>);

// Empty element_type for EBO collapse.
static_assert(std::is_empty_v<consistency::EventualTier::element_type>);
static_assert(std::is_empty_v<consistency::ReadYourWritesTier::element_type>);
static_assert(std::is_empty_v<consistency::CausalPrefixTier::element_type>);
static_assert(std::is_empty_v<consistency::BoundedStalenessTier::element_type>);
static_assert(std::is_empty_v<consistency::StrongTier::element_type>);

// EXHAUSTIVE lattice-axiom + distributivity coverage over
// (Consistency)³ = 125 triples each.  Both verifiers extracted into
// ChainLattice.h (audit Tier-2 dedup) — the helpers handle reflection
// over the underlying enum, so adding a new Consistency tier auto-
// extends coverage with no per-lattice code change.
static_assert(verify_chain_lattice_exhaustive<ConsistencyLattice>(),
    "ConsistencyLattice's chain-order lattice axioms must hold at "
    "every (Consistency)³ triple — failure indicates a defect in "
    "leq/join/meet or in the underlying enum encoding.");
static_assert(verify_chain_lattice_distributive_exhaustive<ConsistencyLattice>(),
    "ConsistencyLattice's chain order must satisfy distributivity at "
    "every (Consistency)³ triple — a chain order always does, so "
    "failure would indicate a defect in join or meet.");

// Direct order witnesses — the entire chain is increasing.
static_assert( ConsistencyLattice::leq(Consistency::EVENTUAL,          Consistency::READ_YOUR_WRITES));
static_assert( ConsistencyLattice::leq(Consistency::READ_YOUR_WRITES,  Consistency::CAUSAL_PREFIX));
static_assert( ConsistencyLattice::leq(Consistency::CAUSAL_PREFIX,     Consistency::BOUNDED_STALENESS));
static_assert( ConsistencyLattice::leq(Consistency::BOUNDED_STALENESS, Consistency::STRONG));
static_assert( ConsistencyLattice::leq(Consistency::EVENTUAL,          Consistency::STRONG));   // transitive endpoints
static_assert(!ConsistencyLattice::leq(Consistency::STRONG,            Consistency::EVENTUAL));
static_assert(!ConsistencyLattice::leq(Consistency::CAUSAL_PREFIX,     Consistency::READ_YOUR_WRITES));

// Pin bottom / top to the chain endpoints.
static_assert(ConsistencyLattice::bottom() == Consistency::EVENTUAL);
static_assert(ConsistencyLattice::top()    == Consistency::STRONG);

// Join strengthens (max); meet weakens (min).
static_assert(ConsistencyLattice::join(Consistency::EVENTUAL,         Consistency::STRONG)        == Consistency::STRONG);
static_assert(ConsistencyLattice::join(Consistency::READ_YOUR_WRITES, Consistency::CAUSAL_PREFIX) == Consistency::CAUSAL_PREFIX);
static_assert(ConsistencyLattice::meet(Consistency::EVENTUAL,         Consistency::STRONG)        == Consistency::EVENTUAL);
static_assert(ConsistencyLattice::meet(Consistency::CAUSAL_PREFIX,    Consistency::STRONG)        == Consistency::CAUSAL_PREFIX);

// Diagnostic names.
static_assert(ConsistencyLattice::name() == "ConsistencyLattice");
static_assert(consistency::EventualTier::name()         == "ConsistencyLattice::At<EVENTUAL>");
static_assert(consistency::ReadYourWritesTier::name()   == "ConsistencyLattice::At<READ_YOUR_WRITES>");
static_assert(consistency::CausalPrefixTier::name()     == "ConsistencyLattice::At<CAUSAL_PREFIX>");
static_assert(consistency::BoundedStalenessTier::name() == "ConsistencyLattice::At<BOUNDED_STALENESS>");
static_assert(consistency::StrongTier::name()           == "ConsistencyLattice::At<STRONG>");
static_assert(consistency_name(Consistency::EVENTUAL)          == "EVENTUAL");
static_assert(consistency_name(Consistency::READ_YOUR_WRITES)  == "READ_YOUR_WRITES");
static_assert(consistency_name(Consistency::CAUSAL_PREFIX)     == "CAUSAL_PREFIX");
static_assert(consistency_name(Consistency::BOUNDED_STALENESS) == "BOUNDED_STALENESS");
static_assert(consistency_name(Consistency::STRONG)            == "STRONG");

// Reflection-driven coverage check on At<C>::name().
[[nodiscard]] consteval bool every_at_consistency_has_name() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^Consistency));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (ConsistencyLattice::At<([:en:])>::name() ==
            std::string_view{"ConsistencyLattice::At<?>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_at_consistency_has_name(),
    "ConsistencyLattice::At<C>::name() switch missing an arm for at "
    "least one tier — add the arm or the new tier leaks the "
    "'ConsistencyLattice::At<?>' sentinel.");

// Convenience aliases resolve correctly.
static_assert(consistency::EventualTier::tier         == Consistency::EVENTUAL);
static_assert(consistency::ReadYourWritesTier::tier   == Consistency::READ_YOUR_WRITES);
static_assert(consistency::CausalPrefixTier::tier     == Consistency::CAUSAL_PREFIX);
static_assert(consistency::BoundedStalenessTier::tier == Consistency::BOUNDED_STALENESS);
static_assert(consistency::StrongTier::tier           == Consistency::STRONG);

// ── Layout invariants on Graded<...,At<C>,T> ────────────────────────
struct OneByteValue   { char c{0}; };
struct EightByteValue { unsigned long long v{0}; };

// Strong-tier graded value (for TP axis: synchronous all-reduce intermediates).
template <typename T>
using StrongGraded = Graded<ModalityKind::Absolute, consistency::StrongTier, T>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(StrongGraded, OneByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(StrongGraded, EightByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(StrongGraded, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(StrongGraded, double);

// Eventual-tier graded value (for DP axis: bounded-staleness gradient
// aggregates).  Empty grade collapses identically across tiers.
template <typename T>
using EventualGraded = Graded<ModalityKind::Absolute, consistency::EventualTier, T>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(EventualGraded, EightByteValue);

// Mid-tier (causal prefix) — typical CRDT-backed metadata fits here.
template <typename T>
using CausalGraded = Graded<ModalityKind::Absolute, consistency::CausalPrefixTier, T>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(CausalGraded, EightByteValue);

// ── Runtime smoke test ─────────────────────────────────────────────
//
// Per feedback_algebra_runtime_smoke_test_discipline memory: exercise
// lattice ops AND Graded::weaken / compose with non-constant arguments
// at runtime.  Catches consteval-vs-constexpr traps the static_assert
// tests miss.
inline void runtime_smoke_test() {
    // Full ConsistencyLattice ops at runtime.
    Consistency a = Consistency::EVENTUAL;
    Consistency b = Consistency::STRONG;
    [[maybe_unused]] bool        l1   = ConsistencyLattice::leq(a, b);
    [[maybe_unused]] Consistency j1   = ConsistencyLattice::join(a, b);
    [[maybe_unused]] Consistency m1   = ConsistencyLattice::meet(a, b);
    [[maybe_unused]] Consistency bot  = ConsistencyLattice::bottom();
    [[maybe_unused]] Consistency top  = ConsistencyLattice::top();

    // Mid-tier ops — chains through the middle of the lattice.
    Consistency mid = Consistency::CAUSAL_PREFIX;
    [[maybe_unused]] Consistency j2 = ConsistencyLattice::join(mid, b);    // STRONG
    [[maybe_unused]] Consistency m2 = ConsistencyLattice::meet(mid, a);    // EVENTUAL

    // Graded<Absolute, StrongTier, T> at runtime.
    OneByteValue v{42};
    StrongGraded<OneByteValue> initial{v, consistency::StrongTier::bottom()};
    auto widened   = initial.weaken(consistency::StrongTier::top());
    auto composed  = initial.compose(widened);
    auto rv_widen  = std::move(widened).weaken(consistency::StrongTier::top());

    [[maybe_unused]] auto g  = rv_widen.grade();
    [[maybe_unused]] auto vc = composed.peek().c;

    // Conversion: At<Consistency>::element_type → Consistency at runtime.
    consistency::StrongTier::element_type e{};
    [[maybe_unused]] Consistency rec = e;
}

}  // namespace detail::consistency_lattice_self_test

}  // namespace crucible::algebra::lattices
