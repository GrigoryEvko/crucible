#pragma once

// ── crucible::algebra::lattices::WitnessLattice ─────────────────────
//
// Four-element total-order lattice over proof-strength tiers — the
// foundation for safety::Witness<W, T> per the V-053..V-056 substrate
// arc (V-053 lattice, V-054 Graded carrier, V-055 row_hash, V-056
// fixy alias).  Witness encodes the EPISTEMIC CONFIDENCE that a value
// satisfies its claimed invariant; the lattice composes confidence
// strengths across producers and downcasts to weaker tiers without
// loss of soundness.
//
// ── The four tiers ──────────────────────────────────────────────────
//
//     UNWITNESSED        — no proof attached.  Default classification
//                          for any value that has not been threaded
//                          through a witness-producing pipeline.  The
//                          BOTTOM of the lattice; safest for the
//                          producer (no claim made), weakest for the
//                          consumer (nothing guaranteed beyond the
//                          C++ type system's static guarantees).
//     TYPE_CHECKED       — Crucible's safety-wrapper discipline
//                          accepted this value.  All linear / refined
//                          / session-typed / tagged invariants
//                          structurally enforced at compile time
//                          (CLAUDE.md L0 "Correctness Won by Three
//                          Disciplines" — discipline #1: contracts +
//                          wrappers).  Subsumes UNWITNESSED.
//     TEST_PASSED        — measurement gate cleared: cross-vendor
//                          numerics CI (MIMIC.md §41) matrix passes
//                          for this value's recipe tier, MAP-Elites
//                          archive validates against real silicon,
//                          sanitizer suite (ASan / UBSan / TSan) and
//                          property fuzzers green.  Discipline #3 in
//                          the CLAUDE.md framing.  Strictly stronger
//                          than TYPE_CHECKED.
//     FORMALLY_VERIFIED  — mathematical proof: either an internal
//                          small-SMT discharge for integer / Presburger
//                          obligations (deferred — interim contracts-
//                          only) OR an external vendor attestation
//                          (e.g. NV cuDSS-style certified kernels per
//                          FIXY-V-176 mimic/nv/Kernel.h Witness<
//                          FormallyVerified, T> use case).  The TOP
//                          of the lattice; rare in current Crucible,
//                          reserved for components that come with a
//                          paper or a checked machine proof.
//
// ── Algebraic shape ─────────────────────────────────────────────────
//
// Carrier: enum class Witness over the four tiers above.
// Order:   UNWITNESSED ⊑ TYPE_CHECKED ⊑ TEST_PASSED ⊑ FORMALLY_VERIFIED.
//
// Bottom = UNWITNESSED       (weakest — no proof made).
// Top    = FORMALLY_VERIFIED (strongest — formal proof).
// Join   = max               (composing claims STRENGTHENS — if one
//                             source says TYPE_CHECKED and another says
//                             TEST_PASSED, the joint claim is the
//                             stronger TEST_PASSED).
// Meet   = min               (downcasting WEAKENS — a TEST_PASSED
//                             producer feeding a sink asking for
//                             TYPE_CHECKED keeps only the weaker
//                             guarantee).
//
// ── Direction convention (matches ConfLattice / ConsistencyLattice) ─
//
// Stronger proof = higher in the lattice.  `leq(weak, strong) = true`
// reads "the weaker tier is subsumed by the stronger tier" — a
// FORMALLY_VERIFIED witness satisfies any consumer asking for
// TEST_PASSED / TYPE_CHECKED / UNWITNESSED.
//
// ── Modality is Comonad (matches ConfLattice) ───────────────────────
//
// Witness<W, T> uses Comonad form per V-054 task:
//   using Witness<W, T> = Graded<Comonad, WitnessLattice::At<W>, T>;
//
// Comonad counit `extract` is always available — observing a
// witnessed value as plain T is sound because the witness is METADATA
// about the value's epistemic status, NOT a filter that hides the
// value from observation.  This is the OPPOSITE of Secret<T> (whose
// Comonad extract is restricted via the secret_policy::* declassify
// rail): a Witness extract is unrestricted, but MINTING a witness at
// a given tier requires producing the proof (the discipline is at
// the producer side, not the consumer side).
//
// ── At<W>: singleton sub-lattice at a fixed type-level tier ─────────
//
// WitnessLattice::At<Witness::FORMALLY_VERIFIED> is a single-element
// sub-lattice with EMPTY element_type.  Used by safety::Witness<W, T>
// (V-054): a `Witness<formally_verified, T>` value is always at the
// FORMALLY_VERIFIED tier — the tier IS the type parameter, encoded at
// the type level via the template-singleton pattern.  Empty
// element_type + [[no_unique_address]] in Graded gives sizeof(
// Witness<W, T>) == sizeof(T), matching the substrate's zero-overhead
// guarantee that V-054 preserves.
//
//   Axiom coverage:
//     TypeSafe — Witness is a strong enum (`enum class : uint8_t`);
//                conversion to underlying requires std::to_underlying,
//                blocking accidental integer math on tier values.
//     DetSafe — every operation is `constexpr` (NOT `consteval`) so
//                Graded's runtime `pre (L::leq(...))` precondition can
//                fire under the `enforce` contract semantic.
//   Runtime cost:
//     leq / join / meet — single integer compare + select; four-element
//     domain compiles to a 1-byte field with a single branch.  When
//     wrapped at a fixed type-level tier via WitnessLattice::At<W>,
//     the grade EBO-collapses to zero bytes — matching the
//     ConfLattice::At<Conf::Secret> / ConsistencyLattice::At<C> shape.
//
// References:
//   Orchard, Liepelt, Eades (2023).  "Graded Modal Types for
//                                     Integrity and Confidentiality."
//                                     arXiv:2309.04324.
//   Pédrot, Tabareau (2017).  "An Effectful Way to Eliminate
//                              Addiction to Dependence." LICS 2017.
//                              (modality / comonad discipline)
//   CLAUDE.md L0 "Correctness Won by Three Disciplines" — the
//                                     contracts + wrappers + measurement
//                                     framing that anchors the four
//                                     tier names.
//   MIMIC.md §41 — cross-vendor numerics CI matrix (the canonical
//                                     TEST_PASSED gate).
//   FIXY-V-176 — mimic/nv/Kernel.h Witness<FormallyVerified, T> use
//                                     case (the canonical FORMALLY_VERIFIED
//                                     producer).
//
// See ALGEBRA-6 (ConfLattice — Comonad convention) and ALGEBRA-14
// (ConsistencyLattice — chain convention) for the pattern this
// lattice mirrors; ChainLattice.h for the inherited ops.

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/Lattice.h>
#include <crucible/algebra/lattices/ChainLattice.h>

#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::algebra::lattices {

// ── Witness tier ────────────────────────────────────────────────────
enum class Witness : std::uint8_t {
    UNWITNESSED       = 0,    // weakest — no proof attached
    TYPE_CHECKED      = 1,    // wrappers + contracts discipline
    TEST_PASSED       = 2,    // measurement discipline (CI / fuzzers)
    FORMALLY_VERIFIED = 3,    // strongest — mathematical proof
};

// Cardinality + diagnostic name via reflection — auto-bumps on
// future tier extensions; reflection-based name-coverage assertion
// catches missing switch arms.
inline constexpr std::size_t witness_count =
    std::meta::enumerators_of(^^Witness).size();

[[nodiscard]] consteval std::string_view witness_name(Witness w) noexcept {
    switch (w) {
        case Witness::UNWITNESSED:       return "UNWITNESSED";
        case Witness::TYPE_CHECKED:      return "TYPE_CHECKED";
        case Witness::TEST_PASSED:       return "TEST_PASSED";
        case Witness::FORMALLY_VERIFIED: return "FORMALLY_VERIFIED";
        default:                         return std::string_view{"<unknown Witness>"};
    }
}

// ── Full WitnessLattice (chain order) ───────────────────────────────
//
// Inherits leq/join/meet from ChainLatticeOps<Witness> per the
// ChainLattice.h dedup convention (audit Tier-2 dedup; see
// ConsistencyLattice for the same shape).
struct WitnessLattice : ChainLatticeOps<Witness> {
    [[nodiscard]] static constexpr element_type bottom() noexcept {
        return Witness::UNWITNESSED;
    }
    [[nodiscard]] static constexpr element_type top() noexcept {
        return Witness::FORMALLY_VERIFIED;
    }

    [[nodiscard]] static consteval std::string_view name() noexcept {
        return "WitnessLattice";
    }

    // ── At<W>: singleton sub-lattice at a fixed type-level tier ─────
    //
    // Used by safety::Witness<W, T> (V-054):
    //   using Witness<W, T> = Graded<Comonad, WitnessLattice::At<W>, T>;
    //
    // Empty element_type — sizeof(WitnessLattice::At<W>::element_type)
    // collapses via EBO inside Graded, giving the V-054 wrapper
    // sizeof(Witness<W, T>) == sizeof(T).  Mirrors the
    // ConfLattice::At<Conf>::element_type pattern (ConfLattice.h:113).
    template <Witness W>
    struct At {
        struct element_type {
            using witness_value_type = Witness;
            [[nodiscard]] constexpr operator witness_value_type() const noexcept {
                return W;
            }
            [[nodiscard]] constexpr bool operator==(element_type) const noexcept {
                return true;
            }
        };

        static constexpr Witness tier = W;

        [[nodiscard]] static constexpr element_type bottom() noexcept { return {}; }
        [[nodiscard]] static constexpr element_type top()    noexcept { return {}; }
        [[nodiscard]] static constexpr bool         leq(element_type, element_type) noexcept { return true; }
        [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }

        [[nodiscard]] static consteval std::string_view name() noexcept {
            switch (W) {
                case Witness::UNWITNESSED:       return "WitnessLattice::At<UNWITNESSED>";
                case Witness::TYPE_CHECKED:      return "WitnessLattice::At<TYPE_CHECKED>";
                case Witness::TEST_PASSED:       return "WitnessLattice::At<TEST_PASSED>";
                case Witness::FORMALLY_VERIFIED: return "WitnessLattice::At<FORMALLY_VERIFIED>";
                default:                         return "WitnessLattice::At<?>";
            }
        }
    };
};

// ── Convenience aliases ─────────────────────────────────────────────
//
// Suffixed `Tier` to avoid collision with Witness::UNWITNESSED /
// TYPE_CHECKED / TEST_PASSED / FORMALLY_VERIFIED enumerators in user
// code that does `using namespace ...`.  Matches the
// consistency::EventualTier / conf::SecretTier convention.
namespace witness {
    using UnwitnessedTier       = WitnessLattice::At<Witness::UNWITNESSED>;
    using TypeCheckedTier       = WitnessLattice::At<Witness::TYPE_CHECKED>;
    using TestPassedTier        = WitnessLattice::At<Witness::TEST_PASSED>;
    using FormallyVerifiedTier  = WitnessLattice::At<Witness::FORMALLY_VERIFIED>;
}  // namespace witness

// ── Self-test (compile-time + reflection-driven name coverage) ──────
namespace detail::witness_lattice_self_test {

// Cardinality + reflection-based name coverage.
static_assert(witness_count == 4,
    "Witness catalog diverged from {UNWITNESSED, TYPE_CHECKED, "
    "TEST_PASSED, FORMALLY_VERIFIED}; confirm intent.  Adding a tier "
    "between TEST_PASSED and FORMALLY_VERIFIED (e.g. CROSS_VENDOR_"
    "VERIFIED) requires updating the V-054 Witness<> alias' tier "
    "shortcuts AND the V-176 mimic::nv:: producer site.");

[[nodiscard]] consteval bool every_witness_has_name() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^Witness));
    // -Wshadow fires on `template for` bodies because GCC 16 unrolls
    // the loop into successive scopes that each declare the same
    // induction variable; suppress locally for the loop body only.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (witness_name([:en:]) == std::string_view{"<unknown Witness>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_witness_has_name(),
    "witness_name() switch missing arm for at least one Witness tier "
    "— add the arm or the new tier leaks the '<unknown Witness>' "
    "sentinel into runtime observer's debug output.");

// Concept conformance — full lattice + each At<W> sub-lattice.
static_assert(Lattice<WitnessLattice>);
static_assert(BoundedLattice<WitnessLattice>);
static_assert(Lattice<witness::UnwitnessedTier>);
static_assert(Lattice<witness::TypeCheckedTier>);
static_assert(Lattice<witness::TestPassedTier>);
static_assert(Lattice<witness::FormallyVerifiedTier>);
static_assert(BoundedLattice<witness::FormallyVerifiedTier>);

// Negative concept assertions — pin WitnessLattice's character.
static_assert(!UnboundedLattice<WitnessLattice>);
static_assert(!Semiring<WitnessLattice>);

// Empty element_type for EBO collapse — load-bearing for V-054's
// Witness<W, T> zero-overhead guarantee.
static_assert(std::is_empty_v<witness::UnwitnessedTier::element_type>);
static_assert(std::is_empty_v<witness::TypeCheckedTier::element_type>);
static_assert(std::is_empty_v<witness::TestPassedTier::element_type>);
static_assert(std::is_empty_v<witness::FormallyVerifiedTier::element_type>);

// EXHAUSTIVE lattice-axiom + distributivity coverage over
// (Witness)³ = 64 triples each.  Both verifiers extracted into
// ChainLattice.h (audit Tier-2 dedup) — the helpers handle reflection
// over the underlying enum, so adding a new Witness tier auto-extends
// coverage with no per-lattice code change.
static_assert(verify_chain_lattice_exhaustive<WitnessLattice>(),
    "WitnessLattice's chain-order lattice axioms must hold at every "
    "(Witness)³ triple — failure indicates a defect in leq/join/meet "
    "or in the underlying enum encoding.");
static_assert(verify_chain_lattice_distributive_exhaustive<WitnessLattice>(),
    "WitnessLattice's chain order must satisfy distributivity at every "
    "(Witness)³ triple — a chain order always does, so failure would "
    "indicate a defect in join or meet.");

// Direct order witnesses — the entire chain is strictly increasing.
static_assert( WitnessLattice::leq(Witness::UNWITNESSED,  Witness::TYPE_CHECKED));
static_assert( WitnessLattice::leq(Witness::TYPE_CHECKED, Witness::TEST_PASSED));
static_assert( WitnessLattice::leq(Witness::TEST_PASSED,  Witness::FORMALLY_VERIFIED));
static_assert( WitnessLattice::leq(Witness::UNWITNESSED,  Witness::FORMALLY_VERIFIED));  // transitive endpoints
static_assert(!WitnessLattice::leq(Witness::FORMALLY_VERIFIED, Witness::UNWITNESSED));
static_assert(!WitnessLattice::leq(Witness::TEST_PASSED,  Witness::TYPE_CHECKED));
static_assert(!WitnessLattice::leq(Witness::FORMALLY_VERIFIED, Witness::TEST_PASSED));

// Pin bottom / top to the chain endpoints.
static_assert(WitnessLattice::bottom() == Witness::UNWITNESSED);
static_assert(WitnessLattice::top()    == Witness::FORMALLY_VERIFIED);

// Join strengthens (max); meet weakens (min).
static_assert(WitnessLattice::join(Witness::UNWITNESSED,  Witness::FORMALLY_VERIFIED) == Witness::FORMALLY_VERIFIED);
static_assert(WitnessLattice::join(Witness::TYPE_CHECKED, Witness::TEST_PASSED)        == Witness::TEST_PASSED);
static_assert(WitnessLattice::meet(Witness::UNWITNESSED,  Witness::FORMALLY_VERIFIED) == Witness::UNWITNESSED);
static_assert(WitnessLattice::meet(Witness::TEST_PASSED,  Witness::FORMALLY_VERIFIED) == Witness::TEST_PASSED);

// Diagnostic names — full lattice + per-tier At<W>::name() coverage.
static_assert(WitnessLattice::name() == "WitnessLattice");
static_assert(witness::UnwitnessedTier::name()       == "WitnessLattice::At<UNWITNESSED>");
static_assert(witness::TypeCheckedTier::name()       == "WitnessLattice::At<TYPE_CHECKED>");
static_assert(witness::TestPassedTier::name()        == "WitnessLattice::At<TEST_PASSED>");
static_assert(witness::FormallyVerifiedTier::name()  == "WitnessLattice::At<FORMALLY_VERIFIED>");
static_assert(witness_name(Witness::UNWITNESSED)       == "UNWITNESSED");
static_assert(witness_name(Witness::TYPE_CHECKED)      == "TYPE_CHECKED");
static_assert(witness_name(Witness::TEST_PASSED)       == "TEST_PASSED");
static_assert(witness_name(Witness::FORMALLY_VERIFIED) == "FORMALLY_VERIFIED");

// Reflection-driven coverage check on At<W>::name().
[[nodiscard]] consteval bool every_at_witness_has_name() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^Witness));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (WitnessLattice::At<([:en:])>::name() ==
            std::string_view{"WitnessLattice::At<?>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_at_witness_has_name(),
    "WitnessLattice::At<W>::name() switch missing an arm for at least "
    "one tier — add the arm or the new tier leaks the "
    "'WitnessLattice::At<?>' sentinel.");

// Convenience aliases resolve correctly.
static_assert(witness::UnwitnessedTier::tier      == Witness::UNWITNESSED);
static_assert(witness::TypeCheckedTier::tier      == Witness::TYPE_CHECKED);
static_assert(witness::TestPassedTier::tier       == Witness::TEST_PASSED);
static_assert(witness::FormallyVerifiedTier::tier == Witness::FORMALLY_VERIFIED);

// At<W>::element_type → Witness conversion recovers the type-level tier.
static_assert(static_cast<Witness>(witness::UnwitnessedTier::element_type{})      == Witness::UNWITNESSED);
static_assert(static_cast<Witness>(witness::TypeCheckedTier::element_type{})      == Witness::TYPE_CHECKED);
static_assert(static_cast<Witness>(witness::TestPassedTier::element_type{})       == Witness::TEST_PASSED);
static_assert(static_cast<Witness>(witness::FormallyVerifiedTier::element_type{}) == Witness::FORMALLY_VERIFIED);

// ── Layout invariants on Graded<Comonad, At<W>, T> ──────────────────
//
// Mirrors ConfLattice's SecretGraded pattern (ConfLattice.h:263-272)
// — pins the V-054 Witness<W, T> wrapper's zero-overhead guarantee
// across arithmetic and aggregate payload types.  Critical for
// FIXY-V-176 (mimic/nv/Kernel.h Witness<FormallyVerified,
// CompiledKernel*>) where the witness must not bloat the kernel
// pointer in the KernelCache slot.
struct OneByteValue   { char c{0}; };
struct EightByteValue { unsigned long long v{0}; };

template <typename T>
using FormallyVerifiedGraded =
    Graded<ModalityKind::Comonad, witness::FormallyVerifiedTier, T>;

CRUCIBLE_GRADED_LAYOUT_INVARIANT(FormallyVerifiedGraded, OneByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(FormallyVerifiedGraded, EightByteValue);
// Arithmetic-T witnesses — pin macro correctness across the trivially-
// default-constructible-T axis (matches ConfLattice.h Tier-2 audit
// discipline).  Critical because Witness<TYPE_CHECKED, int> is a
// likely V-054 use case for tier-tagged counters.
CRUCIBLE_GRADED_LAYOUT_INVARIANT(FormallyVerifiedGraded, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(FormallyVerifiedGraded, double);

// Tier-tagged counter — same payload at a weaker tier, verifies the
// alias works at every tier in the chain (not just the top).
template <typename T>
using TestPassedGraded =
    Graded<ModalityKind::Comonad, witness::TestPassedTier, T>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(TestPassedGraded, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(TestPassedGraded, EightByteValue);

// Bottom-tier verification — UNWITNESSED tier must compose cleanly
// with the carrier even though it claims nothing; load-bearing for
// the V-054 default-witness ergonomic.
template <typename T>
using UnwitnessedGraded =
    Graded<ModalityKind::Comonad, witness::UnwitnessedTier, T>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(UnwitnessedGraded, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(UnwitnessedGraded, EightByteValue);

// ── Runtime smoke test ──────────────────────────────────────────────
//
// Per feedback_algebra_runtime_smoke_test_discipline memory: exercise
// lattice ops AND Graded::weaken / compose / extract with non-constant
// arguments at runtime.  Catches consteval-vs-constexpr traps the
// static_assert tests miss; per feedback_header_only_static_assert_
// blind_spot memory, the sentinel TU in test/ forces the bodies
// through under project warnings-as-errors.
inline void runtime_smoke_test() {
    // Full WitnessLattice ops at runtime.
    Witness a = Witness::UNWITNESSED;
    Witness b = Witness::FORMALLY_VERIFIED;
    [[maybe_unused]] bool    l1   = WitnessLattice::leq(a, b);
    [[maybe_unused]] Witness j1   = WitnessLattice::join(a, b);
    [[maybe_unused]] Witness m1   = WitnessLattice::meet(a, b);
    [[maybe_unused]] Witness bot  = WitnessLattice::bottom();
    [[maybe_unused]] Witness top  = WitnessLattice::top();

    // Mid-tier ops — chains through the middle of the lattice.
    Witness mid = Witness::TEST_PASSED;
    [[maybe_unused]] Witness j2 = WitnessLattice::join(mid, b);    // FORMALLY_VERIFIED
    [[maybe_unused]] Witness m2 = WitnessLattice::meet(mid, a);    // UNWITNESSED

    // Graded<Comonad, FormallyVerifiedTier, T> at runtime.
    OneByteValue v{42};
    FormallyVerifiedGraded<OneByteValue> initial{
        v, witness::FormallyVerifiedTier::bottom()};
    auto widened   = initial.weaken(witness::FormallyVerifiedTier::top());
    auto composed  = initial.compose(widened);
    auto rv_widen  = std::move(widened).weaken(
                         witness::FormallyVerifiedTier::top());

    // Comonad counit (extract) — always available, observing the
    // value as plain T does NOT require declassifying the witness.
    auto extracted = std::move(composed).extract();

    [[maybe_unused]] auto g  = rv_widen.grade();
    [[maybe_unused]] auto vc = extracted.c;

    // Conversion: At<Witness::FORMALLY_VERIFIED>::element_type →
    // Witness at runtime.  Mirrors the ConfLattice / ConsistencyLattice
    // pattern for downstream diagnostic / serialization paths.
    witness::FormallyVerifiedTier::element_type e{};
    [[maybe_unused]] Witness recovered = e;
}

}  // namespace detail::witness_lattice_self_test

}  // namespace crucible::algebra::lattices
