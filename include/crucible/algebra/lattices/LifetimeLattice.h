#pragma once

// ── crucible::algebra::lattices::LifetimeLattice ────────────────────
//
// Three-element total-order lattice over the durability scope of a
// piece of session-attached state.  The grading axis underlying §16
// SessionOpaqueState<T, Lifetime> from 25_04_2026.md:
//
//     using SessionOpaqueState<T, L> = Graded<Comonad, LifetimeLattice, T>;
//
// where the type-level Lifetime parameter pins the value's scope:
//
//     PER_REQUEST  — destroyed on session close (per-request KV
//                    metadata, per-decode token state, per-call
//                    coroutine frame).
//     PER_PROGRAM  — shared across sibling sessions of the same
//                    program (tool-call cache, prefix cache,
//                    grammar PDA stack).
//     PER_FLEET    — Raft-replicated across the Canopy fleet
//                    (Meridian calibration artifacts, Cipher cold
//                    tier metadata, federated kernel cache).
//
// ── Algebraic shape ─────────────────────────────────────────────────
//
// Carrier: enum class Lifetime ∈ {PER_REQUEST, PER_PROGRAM, PER_FLEET}.
// Order:   PER_REQUEST ⊑ PER_PROGRAM ⊑ PER_FLEET (chain order).
//
// Bottom = PER_REQUEST (the smallest scope — easiest to provide,
//                       most permissive about what fits).
// Top    = PER_FLEET   (the largest scope — hardest to provide,
//                       fully subsumes shorter scopes).
// Join   = max         (the union of two requirements is the longer
//                       lifetime — what subsumes both).
// Meet   = min         (the intersection of two providers is the
//                       shorter lifetime — what either guarantees).
//
// ── Direction convention (matches ConfLattice / TrustLattice) ───────
//
// Crucible's grading lattices uniformly orient `bottom = least
// restrictive` and `top = most restrictive`; `leq(a, b)` reads "a's
// requirement is subsumed by b's guarantee".  For Lifetime:
//
//   leq(PER_REQUEST, PER_FLEET) = true
//
// because a fleet-scoped value LIVES THROUGH the duration any
// request-scoped consumer would observe it for — the fleet provider
// satisfies the request consumer's lifetime requirement.
//
// The §16.4 phrase "a fleet-scoped value can be read as program-
// scoped, but not vice versa" describes the COMONAD COUNIT semantics
// (Graded::extract drops the grade entirely; a wrapper above can
// expose a `narrow_to<Lifetime>` that constructs a fresh value at a
// shorter grade).  The COUNIT direction is orthogonal to lattice ⊑;
// both views — algebraic subsumption and comonadic narrowing — are
// available to the wrapper above this lattice.
//
//   Axiom coverage:
//     TypeSafe — Lifetime is a strong enum (`enum class : uint8_t`);
//                conversion to underlying requires `std::to_underlying`.
//     DetSafe — every operation is `constexpr` (NOT `consteval`) so
//                Graded's runtime `pre (L::leq(...))` precondition can
//                fire under the `enforce` contract semantic.
//   Runtime cost:
//     leq / join / meet — single integer compare and a select; the
//     three-element domain compiles to a 1-byte field with a single
//     branch.  When wrapped at a fixed type-level lifetime via a
//     future `LifetimeLattice::At<Lifetime::PER_FLEET>` sub-lattice
//     (the conf::SecretTier pattern from ConfLattice), the grade
//     EBO-collapses to zero bytes.
//
// ── At<L> singleton sub-lattice ─────────────────────────────────────
//
// Mirrors ConfLattice::At<Conf>: a per-Lifetime singleton sub-lattice
// with empty element_type, used when a wrapper's lifetime is fixed at
// the type level.  `SessionOpaqueState<T, PER_FLEET>` would map to
// `Graded<Comonad, LifetimeLattice::At<Lifetime::PER_FLEET>, T>` and
// pay zero runtime overhead — the single-classification regime that
// Secret<T> already exploits via conf::SecretTier.
//
// See ALGEBRA-14 (#459), ALGEBRA-2 (Lattice.h) for the verifier
// helpers; ALGEBRA-6 (ConfLattice) for the convention this lattice
// mirrors; 25_04_2026.md §16 for the SessionOpaqueState use case
// that motivated this lattice.

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/Lattice.h>
#include <crucible/algebra/lattices/ChainLattice.h>

#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::algebra::lattices {

// ── Lifetime durability scope ───────────────────────────────────────
enum class Lifetime : std::uint8_t {
    PER_REQUEST = 0,    // destroyed on session close
    PER_PROGRAM = 1,    // shared across sibling sessions of the same program
    PER_FLEET   = 2,    // Raft-replicated across the Canopy fleet
};

// Cardinality + diagnostic name via reflection — auto-bumps when
// future Lifetime variants ship; the every_lifetime_has_name self-
// test catches a missing switch arm before the unknown sentinel
// leaks into production diagnostics.
inline constexpr std::size_t lifetime_count =
    std::meta::enumerators_of(^^Lifetime).size();

[[nodiscard]] consteval std::string_view lifetime_name(Lifetime l) noexcept {
    switch (l) {
        case Lifetime::PER_REQUEST: return "PER_REQUEST";
        case Lifetime::PER_PROGRAM: return "PER_PROGRAM";
        case Lifetime::PER_FLEET:   return "PER_FLEET";
        default:                    return std::string_view{"<unknown Lifetime>"};
    }
}

// ── Full LifetimeLattice (chain order) ──────────────────────────────
//
// Inherits leq/join/meet from ChainLatticeOps<Lifetime> — those three
// methods are byte-identical across every chain-order lattice over an
// enum class, factored out per the audit Tier-2 dedup.
struct LifetimeLattice : ChainLatticeOps<Lifetime> {
    [[nodiscard]] static constexpr element_type bottom() noexcept {
        return Lifetime::PER_REQUEST;
    }
    [[nodiscard]] static constexpr element_type top() noexcept {
        return Lifetime::PER_FLEET;
    }

    [[nodiscard]] static consteval std::string_view name() noexcept {
        return "LifetimeLattice";
    }

    // ── At<L>: singleton sub-lattice at a fixed type-level scope ────
    //
    // Used by SessionOpaqueState<T, Lifetime> per 25_04_2026.md §16:
    //   using SessionOpaqueState<T, PER_FLEET> =
    //       Graded<Comonad, LifetimeLattice::At<Lifetime::PER_FLEET>, T>;
    //
    // Empty element_type collapses via EBO; sizeof matches the bare T.
    template <Lifetime L>
    struct At {
        struct element_type {
            using lifetime_value_type = Lifetime;
            [[nodiscard]] constexpr operator lifetime_value_type() const noexcept {
                return L;
            }
            [[nodiscard]] constexpr bool operator==(element_type) const noexcept {
                return true;
            }
        };

        static constexpr Lifetime scope = L;

        [[nodiscard]] static constexpr element_type bottom() noexcept { return {}; }
        [[nodiscard]] static constexpr element_type top()    noexcept { return {}; }
        [[nodiscard]] static constexpr bool         leq(element_type, element_type) noexcept { return true; }
        [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }

        [[nodiscard]] static consteval std::string_view name() noexcept {
            switch (L) {
                case Lifetime::PER_REQUEST: return "LifetimeLattice::At<PER_REQUEST>";
                case Lifetime::PER_PROGRAM: return "LifetimeLattice::At<PER_PROGRAM>";
                case Lifetime::PER_FLEET:   return "LifetimeLattice::At<PER_FLEET>";
                default:                    return "LifetimeLattice::At<?>";
            }
        }
    };
};

// ── Convenience aliases ─────────────────────────────────────────────
//
// Avoid `using namespace ...` collisions with the underlying enumerators
// by suffixing with `Tier`, mirroring ConfLattice::PublicTier /
// SecretTier.
namespace lifetime {
    using PerRequestTier = LifetimeLattice::At<Lifetime::PER_REQUEST>;
    using PerProgramTier = LifetimeLattice::At<Lifetime::PER_PROGRAM>;
    using PerFleetTier   = LifetimeLattice::At<Lifetime::PER_FLEET>;
}  // namespace lifetime

// ── Self-test ───────────────────────────────────────────────────────
namespace detail::lifetime_lattice_self_test {

// Cardinality + reflection-based name coverage.
static_assert(lifetime_count == 3,
    "Lifetime catalog diverged from {PER_REQUEST, PER_PROGRAM, PER_FLEET}; "
    "confirm intent and update the BatchPolicy / SessionOpaqueState callers.");

[[nodiscard]] consteval bool every_lifetime_has_name() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^Lifetime));
    // -Wshadow fires on `template for` bodies because GCC 16 unrolls
    // the loop into successive scopes that each declare the same
    // induction variable; suppress locally for the loop body only.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (lifetime_name([:en:]) == std::string_view{"<unknown Lifetime>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_lifetime_has_name(),
    "lifetime_name() switch missing arm for at least one Lifetime — "
    "add the arm or the new scope leaks the '<unknown Lifetime>' "
    "sentinel into Augur's debug output.");

// Concept conformance — full lattice + each At<L> sub-lattice.
static_assert(Lattice<LifetimeLattice>);
static_assert(BoundedLattice<LifetimeLattice>);
static_assert(Lattice<LifetimeLattice::At<Lifetime::PER_REQUEST>>);
static_assert(Lattice<LifetimeLattice::At<Lifetime::PER_PROGRAM>>);
static_assert(Lattice<LifetimeLattice::At<Lifetime::PER_FLEET>>);
static_assert(BoundedLattice<LifetimeLattice::At<Lifetime::PER_FLEET>>);

// Negative concept assertions — pin LifetimeLattice's character.
static_assert(!UnboundedLattice<LifetimeLattice>);
static_assert(!Semiring<LifetimeLattice>);

// Empty element_type for EBO collapse — load-bearing for the
// SessionOpaqueState<T, At<...>> zero-overhead guarantee.
static_assert(std::is_empty_v<LifetimeLattice::At<Lifetime::PER_REQUEST>::element_type>);
static_assert(std::is_empty_v<LifetimeLattice::At<Lifetime::PER_PROGRAM>::element_type>);
static_assert(std::is_empty_v<LifetimeLattice::At<Lifetime::PER_FLEET>::element_type>);

// EXHAUSTIVE lattice-axiom + distributivity coverage over (Lifetime)³
// = 27 triples each.  Both verifiers are extracted into ChainLattice.h
// (audit Tier-2 dedup) — pass the per-lattice ChainLattice as the
// template argument; reflection-driven enumeration handles the rest.
static_assert(verify_chain_lattice_exhaustive<LifetimeLattice>(),
    "LifetimeLattice's chain-order lattice axioms must hold at every "
    "(Lifetime)³ triple — failure indicates a defect in leq/join/meet "
    "or in the underlying enum encoding.");
static_assert(verify_chain_lattice_distributive_exhaustive<LifetimeLattice>(),
    "LifetimeLattice's chain order must satisfy distributivity at "
    "every (Lifetime)³ triple — a chain order always does, so failure "
    "would indicate a defect in join or meet.");

// Direct order witnesses — every level subsumed by the next.
static_assert( LifetimeLattice::leq(Lifetime::PER_REQUEST, Lifetime::PER_PROGRAM));
static_assert( LifetimeLattice::leq(Lifetime::PER_PROGRAM, Lifetime::PER_FLEET));
static_assert( LifetimeLattice::leq(Lifetime::PER_REQUEST, Lifetime::PER_FLEET));   // transitive
static_assert(!LifetimeLattice::leq(Lifetime::PER_FLEET,   Lifetime::PER_REQUEST));
static_assert(!LifetimeLattice::leq(Lifetime::PER_FLEET,   Lifetime::PER_PROGRAM));
static_assert(!LifetimeLattice::leq(Lifetime::PER_PROGRAM, Lifetime::PER_REQUEST));

// Pin bottom / top to the chain endpoints.
static_assert(LifetimeLattice::bottom() == Lifetime::PER_REQUEST);
static_assert(LifetimeLattice::top()    == Lifetime::PER_FLEET);

// Join raises scope (max); meet narrows scope (min).
static_assert(LifetimeLattice::join(Lifetime::PER_REQUEST, Lifetime::PER_FLEET)   == Lifetime::PER_FLEET);
static_assert(LifetimeLattice::join(Lifetime::PER_PROGRAM, Lifetime::PER_REQUEST) == Lifetime::PER_PROGRAM);
static_assert(LifetimeLattice::meet(Lifetime::PER_REQUEST, Lifetime::PER_FLEET)   == Lifetime::PER_REQUEST);
static_assert(LifetimeLattice::meet(Lifetime::PER_PROGRAM, Lifetime::PER_FLEET)   == Lifetime::PER_PROGRAM);

// Diagnostic names.
static_assert(LifetimeLattice::name() == "LifetimeLattice");
static_assert(LifetimeLattice::At<Lifetime::PER_REQUEST>::name() == "LifetimeLattice::At<PER_REQUEST>");
static_assert(LifetimeLattice::At<Lifetime::PER_PROGRAM>::name() == "LifetimeLattice::At<PER_PROGRAM>");
static_assert(LifetimeLattice::At<Lifetime::PER_FLEET>::name()   == "LifetimeLattice::At<PER_FLEET>");
static_assert(lifetime_name(Lifetime::PER_REQUEST) == "PER_REQUEST");
static_assert(lifetime_name(Lifetime::PER_PROGRAM) == "PER_PROGRAM");
static_assert(lifetime_name(Lifetime::PER_FLEET)   == "PER_FLEET");

// Reflection-driven coverage check on At<L>::name() — same discipline
// as ConfLattice's every_at_conf_has_name.
[[nodiscard]] consteval bool every_at_lifetime_has_name() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^Lifetime));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (LifetimeLattice::At<([:en:])>::name() ==
            std::string_view{"LifetimeLattice::At<?>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_at_lifetime_has_name(),
    "LifetimeLattice::At<L>::name() switch missing an arm for at least "
    "one Lifetime — add the arm or the new scope leaks the "
    "'LifetimeLattice::At<?>' sentinel.");

// Convenience aliases resolve correctly.
static_assert(lifetime::PerRequestTier::scope == Lifetime::PER_REQUEST);
static_assert(lifetime::PerProgramTier::scope == Lifetime::PER_PROGRAM);
static_assert(lifetime::PerFleetTier::scope   == Lifetime::PER_FLEET);

// ── Layout invariants on Graded<...,At<L>,T> ────────────────────────
//
// Every concrete SessionOpaqueState<T, At<...>> instantiation must
// EBO-collapse the empty grade.  Witnesses pin the contract for the
// trivially-default-constructible-T axis (audit drop) and for an
// 8-byte payload (typical pointer-sized OpaqueState).
struct OneByteValue   { char c{0}; };
struct EightByteValue { unsigned long long v{0}; };

template <typename T>
using FleetOpaque = Graded<ModalityKind::Comonad, lifetime::PerFleetTier, T>;

CRUCIBLE_GRADED_LAYOUT_INVARIANT(FleetOpaque, OneByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(FleetOpaque, EightByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(FleetOpaque, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(FleetOpaque, double);

// Per-tier instantiation also collapses — confirms the EBO discipline
// holds across every Lifetime variant, not just PER_FLEET.
template <typename T>
using ProgramOpaque = Graded<ModalityKind::Comonad, lifetime::PerProgramTier, T>;
template <typename T>
using RequestOpaque = Graded<ModalityKind::Comonad, lifetime::PerRequestTier, T>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(ProgramOpaque, EightByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(RequestOpaque, EightByteValue);

// ── Runtime smoke test ─────────────────────────────────────────────
//
// Per feedback_algebra_runtime_smoke_test_discipline memory: exercise
// lattice ops AND Graded::weaken / compose / extract with non-constant
// arguments at runtime.  Catches consteval-vs-constexpr traps the
// pure static_assert tests miss.
inline void runtime_smoke_test() {
    // Full LifetimeLattice ops at runtime.
    Lifetime a = Lifetime::PER_REQUEST;
    Lifetime b = Lifetime::PER_FLEET;
    [[maybe_unused]] bool     l1   = LifetimeLattice::leq(a, b);
    [[maybe_unused]] Lifetime j1   = LifetimeLattice::join(a, b);
    [[maybe_unused]] Lifetime m1   = LifetimeLattice::meet(a, b);
    [[maybe_unused]] Lifetime bot  = LifetimeLattice::bottom();
    [[maybe_unused]] Lifetime top  = LifetimeLattice::top();

    // Graded<Comonad, At<Fleet>, T> at runtime.
    OneByteValue v{42};
    FleetOpaque<OneByteValue> initial{v, lifetime::PerFleetTier::bottom()};
    auto widened   = initial.weaken(lifetime::PerFleetTier::top());
    auto composed  = initial.compose(widened);
    auto rv_widen  = std::move(widened).weaken(lifetime::PerFleetTier::top());

    // Comonad counit (extract) — only available because modality is Comonad.
    auto extracted = std::move(composed).extract();

    [[maybe_unused]] auto g  = rv_widen.grade();
    [[maybe_unused]] auto vc = extracted.c;

    // Conversion: At<Lifetime>::element_type → Lifetime at runtime.
    lifetime::PerFleetTier::element_type e{};
    [[maybe_unused]] Lifetime rec = e;
}

}  // namespace detail::lifetime_lattice_self_test

}  // namespace crucible::algebra::lattices
