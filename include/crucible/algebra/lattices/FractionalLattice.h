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
// Rationals carry int64_t num + int64_t den (16 bytes).  Two layers
// protect against signed-integer overflow on cross-product math:
//
//   1. Magnitude bound — `is_well_formed()` requires
//      `0 ≤ num ≤ 2^31` AND `0 < den ≤ 2^31`.  This is the LATTICE-
//      level contract: every Rational consumed by FractionalLattice
//      ops MUST satisfy `is_well_formed()`.  At this bound:
//        • cross-product           a.num × b.den  ≤ 2^62
//        • additive numerator sum  a.num × b.den + b.num × a.den ≤ 2^63 - 1
//        • product denominator     a.den × b.den  ≤ 2^62
//      All three fit in int64_t with margin.  The bound is documented
//      ALSO at the original ALGEBRA-8 design point — typical CSL
//      shares are 1/N for moderate N (1/2, 1/4, ..., 1/2^16) so the
//      bound never binds in real workloads.
//
//   2. Defense-in-depth via __int128 — even if a malicious caller
//      bypasses (1) by constructing a Rational outside the bound, the
//      cross-product comparisons in `operator<=>` and `leq` use
//      __int128 intermediates so the result is mathematically correct
//      for ANY int64 inputs (no signed-overflow UB, no wrong-direction
//      comparison flip).  GCC 16 has unconditional __int128 support;
//      this is zero cost on x86-64 (two-register multiply).  See the
//      AUDIT-FRAC-OVERFLOW commit for the regression probe that
//      motivated this defense.
//
// Without (2), `leq(Rational{2^40, 1}, Rational{1, 2^25})` returned
// `true` due to the cross-product `2^40 × 2^25 = 2^65` wrapping to 0
// — flipping the inequality and silently passing a `Graded::weaken()`
// precondition that should reject the share-strengthening request.
// The bound in (1) prevents the situation from arising in
// well-formed code; (2) ensures DetSafe even when (1) is bypassed.
//
//   Axiom coverage: TypeSafe — Rational is a strong struct with
//                   bounded interpretation enforced by the
//                   is_well_formed() invariant.  DetSafe — every
//                   operation is constexpr, deterministic, and
//                   overflow-free under (1)+(2); no FP arithmetic.
//                   MemSafe — no heap, no aliased state.
//   Runtime cost:   16 bytes per share + cross-product multiplications
//                   on lattice ops.  __int128 cross-product is two
//                   IMUL on x86-64 vs one for int64 — negligible at
//                   SharedPermission's coarse-grained refcount cycle.
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
// CSL [0, 1] share invariant: 0 ≤ num ≤ 2^31 AND 0 < den ≤ 2^31.
// This is the CONTRACT for every Rational consumed by
// FractionalLattice.  Violating it (e.g. constructing
// `Rational{1, -2}`, `Rational{1, 0}`, or `Rational{2^40, 1}`) is a
// boundary-discipline error; `is_well_formed()` gives the discoverable
// check.  FractionalLattice ops `contract_assert` is_well_formed() at
// every boundary AND use __int128 cross-products as defense-in-depth
// so the comparison result is mathematically correct even when a
// caller bypasses the contract.  The NSDMI default {0, 1} is
// well-formed by construction.
struct Rational {
    std::int64_t num{0};
    std::int64_t den{1};

    // ── Magnitude bound for the well-formed CSL share invariant ─────
    //
    // 2^31 chosen so every cross-product (a.num × b.den, a.den × b.den,
    // and the additive sum a.num × b.den + b.num × a.den) fits in
    // int64_t without wrap.  Real CSL shares (1/N for moderate N)
    // never approach this bound; values up to 2^31 cover any
    // refcount-style permission split practical applications need.
    static constexpr std::int64_t MAX_SAFE_MAGNITUDE = std::int64_t{1} << 31;

    // CSL share invariant — must hold for every Rational that flows
    // through FractionalLattice's lattice/semiring ops.  Bounded above
    // by MAX_SAFE_MAGNITUDE so cross-product arithmetic in
    // FractionalLattice's operations stays within int64_t range (the
    // operator<=> path additionally widens to __int128 as
    // defense-in-depth — see file header comment).
    [[nodiscard]] constexpr bool is_well_formed() const noexcept {
        return den > 0
            && num >= 0
            && num <= MAX_SAFE_MAGNITUDE
            && den <= MAX_SAFE_MAGNITUDE;
    }

    // ── int128 typedef for cross-product defense-in-depth ──────────
    //
    // GCC 16 provides `__int128` as an extension; -Werror=pedantic
    // would reject the bare type-id, so we wrap it inside a
    // diagnostic-pragma push/pop pair and expose a single typedef
    // `wide_signed`.  The typedef is private to Rational; the rest of
    // the header refers to it by name and never spells `__int128`
    // directly, keeping the pedantic-suppression scope to one site.
    //
    // Cost: zero — the pragma only affects parsing of the typedef
    // line; emitted code is identical to the bare `__int128` form.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
    using wide_signed = __int128;
#pragma GCC diagnostic pop

    [[nodiscard]] friend constexpr bool operator==(Rational a, Rational b) noexcept {
        // Cross-multiply for equality without simplifying.
        // wide_signed intermediate keeps the comparison sound even
        // for ill-formed inputs (defense-in-depth — the lattice ops
        // contract-assert is_well_formed() at the boundary, but a
        // misbehaving caller that bypasses the contract still gets
        // the mathematically correct answer rather than a UB-induced
        // wrong direction).
        return (static_cast<wide_signed>(a.num) * b.den)
            == (static_cast<wide_signed>(b.num) * a.den);
    }

    [[nodiscard]] friend constexpr auto operator<=>(Rational a, Rational b) noexcept {
        // Cross-multiply for comparison.  wide_signed intermediate
        // ensures correctness for any int64 inputs; assumes positive
        // denominators (is_well_formed() is the discoverable check
        // that lattice ops contract-assert at the boundary).
        return (static_cast<wide_signed>(a.num) * b.den)
           <=> (static_cast<wide_signed>(b.num) * a.den);
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
        // Boundary discipline — every Rational consumed by the lattice
        // ops MUST satisfy is_well_formed() per the file-header
        // contract.  contract_assert fires under enforce semantic
        // (debug, CI); compiles to nothing under ignore (hot-path TUs).
        contract_assert(a.is_well_formed() && b.is_well_formed());
        // Cross-multiply: a/d_a ≤ b/d_b iff a × d_b ≤ b × d_a (for
        // positive denominators).  wide_signed (__int128) intermediate
        // keeps the comparison sound for ANY int64 inputs (defense-in-
        // depth); for is_well_formed() values the result fits
        // comfortably in int64 with margin (≤ 2^62).
        return (static_cast<Rational::wide_signed>(a.num) * b.den)
            <= (static_cast<Rational::wide_signed>(b.num) * a.den);
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
    // to a writer permission).  Cross-products use wide_signed
    // (__int128) intermediates so the additive numerator sum
    // (a × d_b + b × d_a) never wraps; for is_well_formed() inputs
    // each product is ≤ 2^62 and their sum fits comfortably in int64
    // (≤ 2^63 - 1).
    [[nodiscard]] static constexpr element_type add(element_type a, element_type b) noexcept {
        contract_assert(a.is_well_formed() && b.is_well_formed());
        using W = Rational::wide_signed;
        const W num128 = static_cast<W>(a.num) * b.den
                       + static_cast<W>(b.num) * a.den;
        const W den128 = static_cast<W>(a.den) * b.den;
        // For well-formed inputs each product is ≤ 2^62 and the
        // additive numerator sum is ≤ 2^63 - 1, so the truncating cast
        // back to int64 is lossless.  The contract_assert above is
        // the boundary check; the cast is sound under that contract.
        return simplify(Rational{
            static_cast<std::int64_t>(num128),
            static_cast<std::int64_t>(den128),
        });
    }
    // mul: rational multiplication with simplification.  Used to
    // split shares (e.g. mul({1, 2}, {1, 2}) = {1, 4} — split a half
    // into a quarter).  wide_signed intermediates as in `add`.
    [[nodiscard]] static constexpr element_type mul(element_type a, element_type b) noexcept {
        contract_assert(a.is_well_formed() && b.is_well_formed());
        using W = Rational::wide_signed;
        const W num128 = static_cast<W>(a.num) * b.num;
        const W den128 = static_cast<W>(a.den) * b.den;
        return simplify(Rational{
            static_cast<std::int64_t>(num128),
            static_cast<std::int64_t>(den128),
        });
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

// is_well_formed enforces the CSL share invariant
// (0 ≤ num ≤ MAX_SAFE_MAGNITUDE, 0 < den ≤ MAX_SAFE_MAGNITUDE).
static_assert(Rational{}.is_well_formed());            // default {0, 1}
static_assert(Rational{1, 2}.is_well_formed());
static_assert(Rational{0, 1}.is_well_formed());
static_assert(Rational{Rational::MAX_SAFE_MAGNITUDE,
                       Rational::MAX_SAFE_MAGNITUDE}.is_well_formed());
static_assert(!Rational{1, -2}.is_well_formed());      // negative den
static_assert(!Rational{-1, 2}.is_well_formed());      // negative num
static_assert(!Rational{1, 0}.is_well_formed());       // zero den

// AUDIT-FRAC-OVERFLOW regression — values just past the magnitude
// bound are rejected by is_well_formed().  Pre-fix, Rational{2^40, 1}
// was admitted by is_well_formed() and silently produced wrong-
// direction comparisons via the int64 cross-product overflow.  After
// the fix the magnitude bound rules them out structurally AND
// __int128 cross-products inside FractionalLattice ops give the
// mathematically correct comparison even if a caller bypasses the
// contract.
static_assert(!Rational{Rational::MAX_SAFE_MAGNITUDE + 1, 1}.is_well_formed());
static_assert(!Rational{1, Rational::MAX_SAFE_MAGNITUDE + 1}.is_well_formed());
static_assert(!Rational{std::int64_t{1} << 40, 1}.is_well_formed());
static_assert(!Rational{1, std::int64_t{1} << 40}.is_well_formed());
static_assert(!Rational{std::numeric_limits<std::int64_t>::max(), 1}.is_well_formed());

// __int128 defense-in-depth — operator<=> is mathematically correct
// for ANY int64 inputs, including ill-formed values that would
// previously have wrapped via signed-integer overflow.  These
// assertions exercise the path; the lattice ops contract_assert
// is_well_formed() so production callers cannot reach this branch,
// but the soundness of the comparison itself is structural.  The
// canonical regression case from the AUDIT-FRAC-OVERFLOW probe:
// pre-fix, leq(Rational{2^40, 1}, Rational{1, 2^25}) returned `true`
// because 2^40 × 2^25 = 2^65 wrapped to 0 — flipping the comparison.
// __int128 makes the answer correct even outside the contract.
static_assert(Rational{1, std::int64_t{1} << 25}
            < Rational{std::int64_t{1} << 40, 1});
static_assert(!(Rational{std::int64_t{1} << 40, 1}
              < Rational{1, std::int64_t{1} << 25}));

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

    // AUDIT-FRAC-OVERFLOW — exercise the boundary near
    // MAX_SAFE_MAGNITUDE through the runtime path so the
    // contract_assert path is touched under the sentinel TU's enforce
    // semantic.  Both operands are is_well_formed() with denominators
    // at the bound; leq must return the mathematically correct
    // comparison via the __int128 cross-product without overflow.
    // Pre-fix, this path could wrap.
    //
    // Note: `add` and `mul` of two near-bound shares can produce a
    // result whose unsimplified denominator exceeds the bound even
    // though the simplified result might fit.  We only exercise `leq`
    // here so the smoke probe stays focused on the operator-bypass
    // soundness property without entangling simplify-behavior on
    // bound-exceeding intermediates.
    Rational large_lo{1, Rational::MAX_SAFE_MAGNITUDE};
    Rational large_hi{Rational::MAX_SAFE_MAGNITUDE - 1,
                      Rational::MAX_SAFE_MAGNITUDE};
    [[maybe_unused]] bool large_leq = FractionalLattice::leq(large_lo, large_hi);
    [[maybe_unused]] Rational large_join = FractionalLattice::join(large_lo, large_hi);
    [[maybe_unused]] Rational large_meet = FractionalLattice::meet(large_lo, large_hi);

    // Graded<Absolute, FractionalLattice, T> at runtime.
    //
    // Critical invariant: weaken only goes UP the lattice (`L::leq
    // (current_grade, new_grade)`).  The contract pre fires under
    // enforce semantic if you try to weaken to a smaller grade.
    // Build the chain in ascending order: bottom → 3/4 → top.
    OneByteValue v{42};
    SharedPermissionGraded<OneByteValue> initial{v, FractionalLattice::bottom()};
    auto widened    = initial.weaken(Rational{3, 4});                  // 0   → 3/4
    auto widened_max = widened.weaken(FractionalLattice::top());       // 3/4 → 1
    auto composed    = initial.compose(widened_max);                   // join with top
    auto rv_widen    = std::move(widened_max).weaken(FractionalLattice::top());  // 1 → 1 (idempotent reflexive)

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
