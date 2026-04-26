#pragma once

// ── crucible::algebra::lattices::FractionalLattice ──────────────────
//
// ℚ ∩ [0, 1] semiring + bounded lattice — the foundation for
// SharedPermission<Tag> per 25_04_2026.md §2.3:
//
//     using SharedPermission<Tag> =
//         Graded<Absolute, FractionalLattice, Tag>;
//
// References:
//   Boyland (2003).         "Checking Interference with Fractional
//                            Permissions."  ICALP.
//   O'Hearn et al. (2007).  "Resources, Concurrency, and Local Reasoning."
//                            (CSL fractional permissions.)
//
// ── First dynamic-grade lattice in the algebra/ tree ────────────────
//
// Unlike QttSemiring::At, BoolLattice, ConfLattice::At, TrustLattice
// (all per-grade SINGLETONS with empty element_type and EBO-collapsed
// grade), FractionalLattice carries a RUNTIME share at every
// SharedPermission instance.  The share varies per instance: a single
// SharedPermission<TraceRingReaderTag> at 1/4 represents one of four
// equal-sized readers, distinct at runtime from another at 1/2 (one
// of two unequal readers).
//
// Layout consequence: sizeof(SharedPermission<Tag>) ==
//   sizeof(Tag) + sizeof(Rational) + alignment.  NOT zero-overhead.
//   This is structural — the share IS the runtime state, can't EBO
//   away.  CRUCIBLE_GRADED_LAYOUT_INVARIANT does NOT apply here;
//   the per-instance overhead is part of the contract.
//
// ── CSL fractional-permission semantics ─────────────────────────────
//
// Lattice (chain order on rationals in [0, 1]):
//   - leq:    a ⊑ b iff share(a) ≤ share(b)        (more-share is "stronger")
//   - join:   max share                            (combine readers, not splitters)
//   - meet:   min share
//   - bottom = 0/1, top = 1/1
//
// Semiring:
//   - add:    a + b   — share combination (returning shares for write upgrade)
//   - mul:    a × b   — share splitting (split a 1/2 share into two 1/4 shares
//                       via mul(half, half))
//   - zero = 0/1, one = 1/1
//
// Both structures coexist over the same Rational element_type.  The
// Lattice operations are used by Graded::weaken / compose; the
// Semiring operations are used by SharedPermissionPool's split /
// combine machinery (MIGRATE-7 #467).
//
// ── Rational arithmetic + overflow ──────────────────────────────────
//
// Rationals carry int64_t num + int64_t den (16 bytes).  Cross-product
// comparisons (a.num × b.den vs b.num × a.den) and additive cross-
// products fit in int64_t for shares with num, den ≤ 2^31.  Beyond
// that range, overflow is documented as undefined — typical CSL
// fractional permissions never approach the boundary (shares are
// 1/2, 1/4, 1/8, ... or small fractions like 1/N for moderate N).
//
//   Axiom coverage: TypeSafe — Rational is a strong struct with
//                   bounded interpretation.  DetSafe — every operation
//                   is constexpr and deterministic; no FP arithmetic.
//   Runtime cost:   16 bytes per share + cross-product multiplications
//                   on lattice ops.  Acceptable for SharedPermission's
//                   coarse-grained refcount-style usage.
//
// See ALGEBRA-3 (Graded.h), ALGEBRA-2 (Lattice.h);
// MIGRATE-7 (#467) for the SharedPermission<Tag> alias instantiation;
// the existing safety::SharedPermissionPool for the runtime
// refcount machinery that consumes / produces fractional shares.

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/Lattice.h>

#include <compare>
#include <cstdint>
#include <numeric>
#include <string_view>
#include <type_traits>

namespace crucible::algebra::lattices {

// ── Rational ────────────────────────────────────────────────────────
//
// num/den representation of a rational number.  Canonical form not
// required for equality (cross-multiplication handles it), but
// arithmetic operations simplify via std::gcd to keep magnitudes
// bounded.
//
// CSL [0, 1] share invariant: num ≥ 0 AND den > 0.  This is the
// CONTRACT for every Rational consumed by FractionalLattice.
// Violating it (e.g. constructing Rational{1, -2}) yields undefined
// comparison results because operator<=> assumes positive
// denominators.  is_well_formed() verifies the invariant; the
// FractionalLattice ops contract-assert it via the Rational input
// path (NSDMI default {0, 1} is well-formed by construction).
struct Rational {
    std::int64_t num{0};
    std::int64_t den{1};

    // CSL share invariant — must hold for every Rational that flows
    // through FractionalLattice's lattice/semiring ops.
    [[nodiscard]] constexpr bool is_well_formed() const noexcept {
        return den > 0 && num >= 0;
    }

    [[nodiscard]] friend constexpr bool operator==(Rational a, Rational b) noexcept {
        // Cross-multiply for equality without simplifying.
        return a.num * b.den == b.num * a.den;
    }

    [[nodiscard]] friend constexpr auto operator<=>(Rational a, Rational b) noexcept {
        // Cross-multiply for comparison.  Assumes both denominators
        // positive (the CSL share invariant); is_well_formed() is the
        // discoverable check.
        return (a.num * b.den) <=> (b.num * a.den);
    }
};

// ── simplify: reduce to canonical form (num and den coprime) ────────
//
// Implementation note: avoids the `-INT64_MIN` UB by passing the
// signed values directly to std::gcd, which per [numeric.ops.gcd]
// operates on the absolute values.  libstdc++'s implementation
// detail computes |M| internally; for CSL share usage (num ≥ 0, den
// > 0) we never approach the INT64_MIN edge.
[[nodiscard]] constexpr Rational simplify(Rational r) noexcept {
    if (r.num == 0) return Rational{0, 1};
    // std::gcd handles negative inputs by computing |M|, |N| internally;
    // for CSL shares (num ≥ 0, den > 0) the inputs are already
    // non-negative so the computation is straightforward.
    auto g = std::gcd(r.num, r.den);
    return Rational{r.num / g, r.den / g};
}

// ── FractionalLattice ───────────────────────────────────────────────
struct FractionalLattice {
    using element_type = Rational;

    // Lattice ops (chain order on rationals in [0, 1]).
    [[nodiscard]] static constexpr element_type bottom() noexcept {
        return Rational{0, 1};
    }
    [[nodiscard]] static constexpr element_type top() noexcept {
        return Rational{1, 1};
    }
    [[nodiscard]] static constexpr bool leq(element_type a, element_type b) noexcept {
        // Cross-multiply: a/d_a ≤ b/d_b iff a*d_b ≤ b*d_a (for positive
        // denominators).
        return a.num * b.den <= b.num * a.den;
    }
    [[nodiscard]] static constexpr element_type join(element_type a, element_type b) noexcept {
        return leq(a, b) ? b : a;  // max share
    }
    [[nodiscard]] static constexpr element_type meet(element_type a, element_type b) noexcept {
        return leq(a, b) ? a : b;  // min share
    }

    // Semiring ops.
    [[nodiscard]] static constexpr element_type zero() noexcept {
        return Rational{0, 1};
    }
    [[nodiscard]] static constexpr element_type one() noexcept {
        return Rational{1, 1};
    }
    // add: rational addition with simplification.  Used to combine
    // shares (e.g. when readers return their fractions to upgrade
    // to a writer permission).
    [[nodiscard]] static constexpr element_type add(element_type a, element_type b) noexcept {
        // a/d_a + b/d_b = (a*d_b + b*d_a) / (d_a * d_b)
        Rational sum{a.num * b.den + b.num * a.den, a.den * b.den};
        return simplify(sum);
    }
    // mul: rational multiplication with simplification.  Used to
    // split shares (e.g. mul({1, 2}, {1, 2}) = {1, 4} — split a half
    // into a quarter).
    [[nodiscard]] static constexpr element_type mul(element_type a, element_type b) noexcept {
        Rational prod{a.num * b.num, a.den * b.den};
        return simplify(prod);
    }

    [[nodiscard]] static consteval std::string_view name() noexcept {
        return "FractionalLattice";
    }
};

// ── Self-test ───────────────────────────────────────────────────────
namespace detail::fractional_lattice_self_test {

// Concept conformance — both Lattice (via leq/join/meet/bottom/top)
// AND Semiring (via add/mul/zero/one + ==).
static_assert(Lattice<FractionalLattice>);
static_assert(BoundedLattice<FractionalLattice>);
static_assert(Semiring<FractionalLattice>);

// Rational has runtime state — 16 bytes, NOT empty.  Documents the
// no-EBO-collapse status that distinguishes FractionalLattice from
// the singleton lattices (QttSemiring::At, BoolLattice, etc.).
static_assert(!std::is_empty_v<Rational>);
static_assert(sizeof(Rational) == 16);
static_assert(alignof(Rational) == 8);

// Rational comparison + ordering.
static_assert(Rational{1, 2} == Rational{2, 4});  // canonical equivalence
static_assert(Rational{1, 4} < Rational{1, 2});
static_assert(Rational{0, 1} == FractionalLattice::bottom());
static_assert(Rational{1, 1} == FractionalLattice::top());

// simplify reduces to coprime form.
static_assert(simplify(Rational{2, 4}) == Rational{1, 2});
static_assert(simplify(Rational{6, 8}) == Rational{3, 4});
static_assert(simplify(Rational{0, 5}) == Rational{0, 1});
static_assert(simplify(Rational{5, 5}) == Rational{1, 1});

// is_well_formed enforces the CSL share invariant (num ≥ 0, den > 0).
static_assert(Rational{}.is_well_formed());            // default {0, 1}
static_assert(Rational{1, 2}.is_well_formed());
static_assert(Rational{0, 1}.is_well_formed());
static_assert(!Rational{1, -2}.is_well_formed());      // negative den
static_assert(!Rational{-1, 2}.is_well_formed());      // negative num
static_assert(!Rational{1, 0}.is_well_formed());       // zero den

// Lattice axioms — spot-check at a representative span of CSL shares
// (0, 1/4, 1/2, 3/4, 1).  Pure spot-check (not exhaustive) because
// FractionalLattice is over INFINITE ℚ ∩ [0, 1] — full coverage
// requires axiom witnesses, not exhaustion.  The verify_*_at helpers
// themselves cover the full axiom family per witness triple.
constexpr Rational r0   = FractionalLattice::bottom();
constexpr Rational r14  = Rational{1, 4};
constexpr Rational r12  = Rational{1, 2};
constexpr Rational r34  = Rational{3, 4};
constexpr Rational r1   = FractionalLattice::top();

static_assert(verify_bounded_lattice_axioms_at<FractionalLattice>(r0,  r0,  r0));
static_assert(verify_bounded_lattice_axioms_at<FractionalLattice>(r0,  r12, r1));
static_assert(verify_bounded_lattice_axioms_at<FractionalLattice>(r14, r12, r34));
static_assert(verify_bounded_lattice_axioms_at<FractionalLattice>(r12, r34, r1));
static_assert(verify_bounded_lattice_axioms_at<FractionalLattice>(r1,  r1,  r1));
static_assert(verify_bounded_lattice_axioms_at<FractionalLattice>(r34, r12, r14));  // descending
static_assert(verify_bounded_lattice_axioms_at<FractionalLattice>(r0,  r14, r1));   // mixed ends

// Semiring axioms — at the same witness span.
static_assert(verify_semiring_axioms_at<FractionalLattice>(r0,  r0,  r0));
static_assert(verify_semiring_axioms_at<FractionalLattice>(r14, r14, r14));
static_assert(verify_semiring_axioms_at<FractionalLattice>(r0,  r12, r1));
static_assert(verify_semiring_axioms_at<FractionalLattice>(r14, r12, r34));
static_assert(verify_semiring_axioms_at<FractionalLattice>(r1,  r1,  r1));

// CSL identities.
static_assert(FractionalLattice::add(r12, r12) == r1,
    "Two halves combine to a full share (split readers → write upgrade).");
static_assert(FractionalLattice::add(r14, r14) == r12,
    "Two quarters combine to a half (partial reader merge).");
static_assert(FractionalLattice::mul(r12, r12) == r14,
    "Half of a half is a quarter (recursive split).");
static_assert(FractionalLattice::mul(r1, r12) == r12,
    "Multiplicative identity: 1 × x = x.");
static_assert(FractionalLattice::mul(r0, r12) == r0,
    "Multiplicative absorption: 0 × x = 0.");
static_assert(FractionalLattice::leq(r14, r12),
    "1/4 ⊑ 1/2 in the chain order on shares.");
static_assert(!FractionalLattice::leq(r12, r14),
    "1/2 ⋢ 1/4 (more share is greater, not less).");
static_assert(FractionalLattice::join(r14, r12) == r12,
    "Join is max — joining readers picks the biggest share.");
static_assert(FractionalLattice::meet(r14, r12) == r14,
    "Meet is min — meeting picks the smallest share.");

// Diagnostic name.
static_assert(FractionalLattice::name() == "FractionalLattice");

// ── Layout — NOT zero-overhead ──────────────────────────────────────
//
// Documents the structural reason FractionalLattice is dynamic-grade:
// Rational is non-empty so [[no_unique_address]] grade_ in Graded
// stores the full 16 bytes.  CRUCIBLE_GRADED_LAYOUT_INVARIANT does
// NOT apply.
struct OneByteValue { char c{0}; };
struct EightByteValue { unsigned long long v{0}; };

template <typename T>
using SharedPermissionGraded =
    Graded<ModalityKind::Absolute, FractionalLattice, T>;

// Verify the runtime overhead is exactly the Rational cost +
// alignment for T.
static_assert(sizeof(SharedPermissionGraded<OneByteValue>) ==
              sizeof(OneByteValue) + sizeof(Rational) + 7,  // 1 + 16 + 7 padding
    "SharedPermission<T> for 1B T should pad to 8B-alignment with 16B Rational.");
static_assert(sizeof(SharedPermissionGraded<EightByteValue>) ==
              sizeof(EightByteValue) + sizeof(Rational),    // 8 + 16 = 24
    "SharedPermission<T> for 8B T has no padding — exactly 24B.");

// ── Runtime smoke test ──────────────────────────────────────────────
//
// Per feedback_algebra_runtime_smoke_test_discipline memory: exercise
// lattice + semiring ops AND Graded operations with non-constant
// arguments — particularly important here because FractionalLattice
// is the FIRST lattice with runtime state, so the
// consteval-vs-constexpr trap would surface immediately.
inline void runtime_smoke_test() {
    // Rationals from non-constant inputs.
    std::int64_t numA = 1, denA = 4;
    std::int64_t numB = 1, denB = 2;
    Rational a{numA, denA};
    Rational b{numB, denB};

    // Lattice ops at runtime.
    [[maybe_unused]] bool     l = FractionalLattice::leq(a, b);
    [[maybe_unused]] Rational j = FractionalLattice::join(a, b);
    [[maybe_unused]] Rational m = FractionalLattice::meet(a, b);

    // Semiring ops at runtime — exercises simplify, gcd, cross-prods.
    [[maybe_unused]] Rational s   = FractionalLattice::add(a, b);  // 1/4 + 1/2 = 3/4
    [[maybe_unused]] Rational p   = FractionalLattice::mul(a, b);  // 1/4 × 1/2 = 1/8
    [[maybe_unused]] Rational ss  = simplify(Rational{numA + numB, denA + denB});

    // Graded<Absolute, FractionalLattice, T> at runtime.
    OneByteValue v{42};
    SharedPermissionGraded<OneByteValue> initial{v, FractionalLattice::bottom()};
    auto widened   = initial.weaken(FractionalLattice::top());
    auto composed  = initial.compose(widened);
    auto rv_widen  = std::move(widened).weaken(Rational{3, 4});

    // Exercise rvalue-compose on a moved-into helper handle so the
    // returned Graded can be consumed without aliasing `rv_widen` or
    // `composed`.  Forces the && overloads of compose+consume through
    // the runtime path.
    SharedPermissionGraded<OneByteValue> for_consume = rv_widen.compose(composed);
    OneByteValue consumed = std::move(for_consume).consume();

    [[maybe_unused]] auto g  = composed.grade();
    [[maybe_unused]] auto v1 = composed.peek().c;
    [[maybe_unused]] auto v2 = consumed.c;
}

}  // namespace detail::fractional_lattice_self_test

}  // namespace crucible::algebra::lattices
