#pragma once

// The fractional permissions of Boyland 2003 and of O'Hearn 2007, as the
// rationals in the unit interval carrying both a bounded lattice and a
// semiring over one element type.  A share is per-instance runtime state, so a
// carrier graded on this lattice pays for it and cannot collapse to its
// payload the way a carrier over an empty grade does.
//
// Two layers keep the cross-product arithmetic clear of signed overflow.
//
// The first is the magnitude bound in is_well_formed, which every value
// entering an operation must satisfy.  Under it a cross-product reaches at most
// two to the sixty-second, as does a product of denominators, but the additive
// numerator sum reaches two to the sixty-third, exactly one past the largest
// representable value.  So add reduces that sum in the wide type before
// narrowing it.
//
// The second is that the comparisons widen unconditionally.  A caller that
// builds a value outside the bound still gets a mathematically correct answer
// instead of a wrapped one.  Without it a comparison of a huge value against a
// tiny one can wrap its cross-product to zero, invert the answer, and admit a
// share-strengthening request that should have been refused.

#include <crucible/algebra/_Graded.h>
#include <crucible/algebra/_Lattice.h>

#include <compare>
#include <cstdint>
#include <numeric>
#include <string_view>
#include <type_traits>

namespace crucible::algebra::lattices {

// A value need not be in canonical form to compare equal, because the
// comparisons cross-multiply.  The arithmetic still reduces, to keep the
// magnitudes inside the bound.
struct Rational {
    std::int64_t num{0};
    std::int64_t den{1};

    // Two to the thirty-first is the largest bound under which every product
    // this header forms stays within the arithmetic described in the file
    // header.  A permission split reaches a denominator of one over a modest
    // count, so the bound never binds in practice.
    static constexpr std::int64_t MAX_SAFE_MAGNITUDE = std::int64_t{1} << 31;

    // Every value entering an operation must satisfy this.
    [[nodiscard]] constexpr bool is_well_formed() const noexcept {
        return den > 0 && num >= 0 && num <= MAX_SAFE_MAGNITUDE && den <= MAX_SAFE_MAGNITUDE;
    }

    // The 128-bit integer is a compiler extension, which the pedantic warning
    // rejects.  Naming it once behind a suppressed pragma keeps that
    // suppression to a single line, and the rest of the header uses the alias.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
    using wide_signed = __int128;
#pragma GCC diagnostic pop

    // Both comparisons widen before multiplying, so a caller that bypasses the
    // well-formedness contract still gets the mathematically correct answer
    // rather than a wrapped one.  Both assume a positive denominator.
    [[nodiscard]] friend constexpr bool operator==(Rational a, Rational b) noexcept {
        return (static_cast<wide_signed>(a.num) * b.den) == (static_cast<wide_signed>(b.num) * a.den);
    }

    [[nodiscard]] friend constexpr auto operator<=>(Rational a, Rational b) noexcept {
        return (static_cast<wide_signed>(a.num) * b.den) <=> (static_cast<wide_signed>(b.num) * a.den);
    }
};

// The standard's greatest common divisor operates on absolute values, which is
// where a most-negative input would be unrepresentable.  A share is
// non-negative with a positive denominator, so that edge is unreachable here.
[[nodiscard]] constexpr Rational simplify(Rational r) noexcept {
    if (r.num == 0) return Rational{0, 1};
    auto g = std::gcd(r.num, r.den);
    return Rational{r.num / g, r.den / g};
}

// Reducing before the narrowing cast, rather than after, is what keeps add
// sound at the top of the bound.  Its unreduced numerator reaches two to the
// sixty-third there, one past the largest representable value.  Casting first
// wraps that to the most negative value and then hands the greatest common
// divisor a magnitude it cannot represent.
//
// Reducing first always fits.  The numerator reaches two to the sixty-third
// only when both operands equal one, and there the denominator is two to the
// sixty-second and divides it, leaving two over one.  In general a numerator at
// that value forces a divisor of at least two to the sixty-second, so the
// reduced numerator is at most two.  Below that value the cast was already
// lossless, and the denominator, a product of two bounded denominators, always
// fits.
[[nodiscard]] constexpr Rational simplify_wide(Rational::wide_signed num, Rational::wide_signed den) noexcept {
    using W = Rational::wide_signed;
    if (num == 0) return Rational{0, 1};
    W a = num < 0 ? -num : num;
    W b = den;
    while (b != 0) {
        const W t = a % b;
        a = b;
        b = t;
    }
    const W g = a;
    return Rational{static_cast<std::int64_t>(num / g), static_cast<std::int64_t>(den / g)};
}

// A larger share is the stronger claim, so it sits higher.  Addition combines
// two shares and multiplication splits one.
struct FractionalLattice {
    using element_type = Rational;

    [[nodiscard]] static constexpr element_type bottom() noexcept { return Rational{0, 1}; }
    [[nodiscard]] static constexpr element_type top() noexcept { return Rational{1, 1}; }
    [[nodiscard]] static constexpr bool leq(element_type a, element_type b) noexcept {
        contract_assert(a.is_well_formed() && b.is_well_formed());
        return (static_cast<Rational::wide_signed>(a.num) * b.den)
            <= (static_cast<Rational::wide_signed>(b.num) * a.den);
    }
    [[nodiscard]] static constexpr element_type join(element_type a, element_type b) noexcept {
        return leq(a, b) ? b : a;
    }
    [[nodiscard]] static constexpr element_type meet(element_type a, element_type b) noexcept {
        return leq(a, b) ? a : b;
    }

    [[nodiscard]] static constexpr element_type zero() noexcept { return Rational{0, 1}; }
    [[nodiscard]] static constexpr element_type one() noexcept { return Rational{1, 1}; }
    // The reduction happens in the wide type, before narrowing, for the reason
    // given at simplify_wide.
    [[nodiscard]] static constexpr element_type add(element_type a, element_type b) noexcept {
        contract_assert(a.is_well_formed() && b.is_well_formed());
        using W = Rational::wide_signed;
        const W num128 = static_cast<W>(a.num) * b.den + static_cast<W>(b.num) * a.den;
        const W den128 = static_cast<W>(a.den) * b.den;
        return simplify_wide(num128, den128);
    }
    // Multiplying two bounded numerators, and two bounded denominators, stays
    // inside the narrow type, so this one narrows before reducing.
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

    [[nodiscard]] static consteval std::string_view name() noexcept { return "FractionalLattice"; }
};

namespace detail::fractional_lattice_self_test {

static_assert(Lattice<FractionalLattice>);
static_assert(BoundedLattice<FractionalLattice>);
static_assert(Semiring<FractionalLattice>);

static_assert(!std::is_empty_v<Rational>);
static_assert(sizeof(Rational) == 16);
static_assert(alignof(Rational) == 8);

static_assert(Rational{1, 2} == Rational{2, 4});
static_assert(Rational{1, 4} < Rational{1, 2});
static_assert(Rational{0, 1} == FractionalLattice::bottom());
static_assert(Rational{1, 1} == FractionalLattice::top());

static_assert(simplify(Rational{2, 4}) == Rational{1, 2});
static_assert(simplify(Rational{6, 8}) == Rational{3, 4});
static_assert(simplify(Rational{0, 5}) == Rational{0, 1});
static_assert(simplify(Rational{5, 5}) == Rational{1, 1});

static_assert(Rational{}.is_well_formed());
static_assert(Rational{1, 2}.is_well_formed());
static_assert(Rational{0, 1}.is_well_formed());
static_assert(Rational{Rational::MAX_SAFE_MAGNITUDE, Rational::MAX_SAFE_MAGNITUDE}.is_well_formed());
static_assert(!Rational{1, -2}.is_well_formed());
static_assert(!Rational{-1, 2}.is_well_formed());
static_assert(!Rational{1, 0}.is_well_formed());

static_assert(!Rational{Rational::MAX_SAFE_MAGNITUDE + 1, 1}.is_well_formed());
static_assert(!Rational{1, Rational::MAX_SAFE_MAGNITUDE + 1}.is_well_formed());
static_assert(!Rational{std::int64_t{1} << 40, 1}.is_well_formed());
static_assert(!Rational{1, std::int64_t{1} << 40}.is_well_formed());
static_assert(!Rational{std::numeric_limits<std::int64_t>::max(), 1}.is_well_formed());

// Both operands here are outside the bound, so no operation would accept them,
// but the comparison itself must still answer correctly.  A narrow
// cross-product of these two wraps to zero and inverts the answer.
static_assert(Rational{1, std::int64_t{1} << 25} < Rational{std::int64_t{1} << 40, 1});
static_assert(!(Rational{std::int64_t{1} << 40, 1} < Rational{1, std::int64_t{1} << 25}));

// The carrier is infinite, so the axioms are witnessed at a span of shares
// rather than exhausted.
constexpr Rational r0 = FractionalLattice::bottom();
constexpr Rational r14 = Rational{1, 4};
constexpr Rational r12 = Rational{1, 2};
constexpr Rational r34 = Rational{3, 4};
constexpr Rational r1 = FractionalLattice::top();

static_assert(verify_bounded_lattice_axioms_at<FractionalLattice>(r0, r0, r0));
static_assert(verify_bounded_lattice_axioms_at<FractionalLattice>(r0, r12, r1));
static_assert(verify_bounded_lattice_axioms_at<FractionalLattice>(r14, r12, r34));
static_assert(verify_bounded_lattice_axioms_at<FractionalLattice>(r12, r34, r1));
static_assert(verify_bounded_lattice_axioms_at<FractionalLattice>(r1, r1, r1));
static_assert(verify_bounded_lattice_axioms_at<FractionalLattice>(r34, r12, r14));
static_assert(verify_bounded_lattice_axioms_at<FractionalLattice>(r0, r14, r1));

static_assert(verify_semiring_axioms_at<FractionalLattice>(r0, r0, r0));
static_assert(verify_semiring_axioms_at<FractionalLattice>(r14, r14, r14));
static_assert(verify_semiring_axioms_at<FractionalLattice>(r0, r12, r1));
static_assert(verify_semiring_axioms_at<FractionalLattice>(r14, r12, r34));
static_assert(verify_semiring_axioms_at<FractionalLattice>(r1, r1, r1));

static_assert(FractionalLattice::add(r12, r12) == r1,
              "Two halves combine to a full share (split readers → write upgrade).");
static_assert(FractionalLattice::add(r14, r14) == r12, "Two quarters combine to a half (partial reader merge).");
static_assert(FractionalLattice::mul(r12, r12) == r14, "Half of a half is a quarter (recursive split).");
static_assert(FractionalLattice::mul(r1, r12) == r12, "Multiplicative identity: 1 × x = x.");
static_assert(FractionalLattice::mul(r0, r12) == r0, "Multiplicative absorption: 0 × x = 0.");
static_assert(FractionalLattice::leq(r14, r12), "1/4 ⊑ 1/2 in the chain order on shares.");
static_assert(!FractionalLattice::leq(r12, r14), "1/2 ⋢ 1/4 (more share is greater, not less).");
static_assert(FractionalLattice::join(r14, r12) == r12, "Join is max — joining readers picks the biggest share.");
static_assert(FractionalLattice::meet(r14, r12) == r14, "Meet is min — meeting picks the smallest share.");

// The two assertions below sit exactly at the top of the bound, where the
// unreduced numerator reaches the value that a narrowing cast would wrap.  They
// are compile-time, so a reduction ordered the other way would not merely give
// a wrong answer here, it would fail to compile.
static_assert(FractionalLattice::add(Rational{Rational::MAX_SAFE_MAGNITUDE, Rational::MAX_SAFE_MAGNITUDE},
                                     Rational{Rational::MAX_SAFE_MAGNITUDE, Rational::MAX_SAFE_MAGNITUDE})
                  == Rational{2, 1},
              "Adding two full shares written at the top of the bound must reduce to "
              "two over one, without overflow.");
static_assert(FractionalLattice::add(Rational{Rational::MAX_SAFE_MAGNITUDE, 1},
                                     Rational{Rational::MAX_SAFE_MAGNITUDE, 1})
                  == Rational{std::int64_t{2} * Rational::MAX_SAFE_MAGNITUDE, 1},
              "Adding two shares at the bound over one must double the numerator, "
              "which still fits.");
// The same sum written in the small form must agree with the bounded form.
static_assert(FractionalLattice::add(FractionalLattice::top(), FractionalLattice::top()) == Rational{2, 1});

static_assert(FractionalLattice::name() == "FractionalLattice");

// The grade is not empty, so the exact-size layout invariant does not apply
// here and the growth is bounded by hand instead.
struct OneByteValue {
    char c{0};
};
struct EightByteValue {
    unsigned long long v{0};
};

template <typename T>
using SharedPermissionGraded = Graded<ModalityKind::Absolute, FractionalLattice, T>;

static_assert(sizeof(SharedPermissionGraded<OneByteValue>) == sizeof(OneByteValue) + sizeof(Rational) + 7,
              "A one-byte payload carrying a share must pad out to the share's "
              "alignment.");
static_assert(sizeof(SharedPermissionGraded<EightByteValue>) == sizeof(EightByteValue) + sizeof(Rational),
              "An eight-byte payload carrying a share must need no padding.");

// Calling each operation on runtime operands catches the defects the
// compile-time assertions above cannot see, such as an inline body that only
// ever instantiates in a consteval context.
inline void runtime_smoke_test() {
    std::int64_t numA = 1, denA = 4;
    std::int64_t numB = 1, denB = 2;
    Rational a{numA, denA};
    Rational b{numB, denB};

    [[maybe_unused]] bool l = FractionalLattice::leq(a, b);
    [[maybe_unused]] Rational j = FractionalLattice::join(a, b);
    [[maybe_unused]] Rational m = FractionalLattice::meet(a, b);

    [[maybe_unused]] Rational s = FractionalLattice::add(a, b);
    [[maybe_unused]] Rational p = FractionalLattice::mul(a, b);
    [[maybe_unused]] Rational ss = simplify(Rational{numA + numB, denA + denB});

    // Only the comparison is exercised at the bound.  Adding or multiplying two
    // shares this large produces an unreduced denominator past the bound even
    // where the reduced result would fit, which would test reduction behaviour
    // rather than the comparison this probe is about.
    Rational large_lo{1, Rational::MAX_SAFE_MAGNITUDE};
    Rational large_hi{Rational::MAX_SAFE_MAGNITUDE - 1, Rational::MAX_SAFE_MAGNITUDE};
    [[maybe_unused]] bool large_leq = FractionalLattice::leq(large_lo, large_hi);
    [[maybe_unused]] Rational large_join = FractionalLattice::join(large_lo, large_hi);
    [[maybe_unused]] Rational large_meet = FractionalLattice::meet(large_lo, large_hi);

    // Weakening only ever moves up the order, so the shares below are built in
    // ascending sequence.  Requesting a smaller grade violates the
    // precondition.
    OneByteValue v{42};
    SharedPermissionGraded<OneByteValue> initial{v, FractionalLattice::bottom()};
    auto widened = initial.weaken(Rational{3, 4});
    auto widened_max = widened.weaken(FractionalLattice::top());
    auto composed = initial.compose(widened_max);
    auto rv_widen = std::move(widened_max).weaken(FractionalLattice::top());

    // Composing into a separate handle lets the result be consumed without
    // aliasing either operand, which is what reaches the rvalue overloads.
    SharedPermissionGraded<OneByteValue> for_consume = rv_widen.compose(composed);
    OneByteValue consumed = std::move(for_consume).consume();

    [[maybe_unused]] auto g = composed.grade();
    [[maybe_unused]] auto v1 = composed.peek().c;
    [[maybe_unused]] auto v2 = consumed.c;
}

}  // namespace detail::fractional_lattice_self_test

}  // namespace crucible::algebra::lattices
