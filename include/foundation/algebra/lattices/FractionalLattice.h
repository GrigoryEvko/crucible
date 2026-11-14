#pragma once

// The fractional permissions of Boyland 2003 and of O'Hearn 2007, as the
// rationals in the unit interval.  They carry a bounded lattice, and a
// partial commutative monoid under add, as in separation logic.  add is
// partial: two shares whose sum is above one have no sum in the carrier, and
// add refuses them.  So the shares are no semiring.  A share is per-instance
// runtime state, so a carrier graded on this lattice pays for it and cannot
// collapse to its payload the way a carrier over an empty grade does.
//
// Two layers keep the cross-product arithmetic clear of signed overflow.
//
// The first is the carrier, is_well_formed, which every value entering an
// operation must satisfy.  Under it a cross-product reaches at most two to
// the sixty-second, as does a product of denominators.  The numerator sum of
// an accepted add is at most the product of the denominators.  add and mul
// still reduce in the wide type before they narrow.  A result whose reduced
// denominator is above the bound is outside the carrier, and the operation
// refuses it with a fatal invariant.  The limit depends on the data, a share
// of a share many times, and not on an error of the caller.  So the refusal
// checks under each contract semantic, and the ignore semantic does not make
// it an assumption.
//
// The second is that the comparisons widen unconditionally.  A caller that
// builds a value outside the bound still gets a mathematically correct answer
// instead of a wrapped one.  Without it a comparison of a huge value against a
// tiny one can wrap its cross-product to zero, invert the answer, and admit a
// share-strengthening request that should have been refused.

#include <foundation/algebra/Graded.h>
#include <foundation/algebra/Lattice.h>
#include <foundation/algebra/lattices/DualLattice.h>
#include <foundation/contracts/Pre.h>

#include <compare>
#include <cstdint>
#include <numeric>
#include <string_view>
#include <type_traits>

namespace foundation::algebra::lattices {

// A value need not be in canonical form to compare equal, because the
// comparisons cross-multiply.  The arithmetic still reduces, to keep the
// magnitudes inside the bound.
struct Rational {
    std::int64_t num{0};
    std::int64_t den{1};

    // Two to the thirty-first is the largest bound under which every product
    // this header forms stays within the arithmetic described in the file
    // header.  A permission split reaches a denominator of one over a modest
    // count, so the bound binds only for a deep chain of splits.
    static constexpr std::int64_t MAX_SAFE_MAGNITUDE = std::int64_t{1} << 31;

    // The carrier: a share in the unit interval, with a positive denominator
    // at most the bound.  Every value entering an operation must satisfy
    // this, and every result of an operation does.
    [[nodiscard]] constexpr bool is_well_formed() const noexcept {
        return den > 0 && den <= MAX_SAFE_MAGNITUDE && num >= 0 && num <= den;
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

// Reduces a fraction of the wide type, and then narrows it.  The reduction
// comes before the narrowing cast.  A cast first would wrap a numerator past
// the narrow type, and then hand the greatest common divisor a magnitude that
// it cannot represent.  For an accepted add, the numerator is at most the
// denominator, and the denominator, a product of two bounded denominators, is
// at most two to the sixty-second.  So each narrowed part fits.
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

    // Graded stores a share through the order dual (DualLattice.h).
    static constexpr ClaimOrientation claim_orientation = ClaimOrientation::stronger_is_higher;

    [[nodiscard]] static constexpr element_type bottom() noexcept { return Rational{0, 1}; }
    [[nodiscard]] static constexpr element_type top() noexcept { return Rational{1, 1}; }
    [[nodiscard]] static constexpr bool leq(element_type a, element_type b) noexcept {
        CRUCIBLE_PRE(a.is_well_formed() && b.is_well_formed());
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

    // True when the sum of two shares is at most one, the domain of add.  The
    // compare is exact in the wide type.
    [[nodiscard]] static constexpr bool is_sum_at_most_one(element_type a, element_type b) noexcept {
        using W = Rational::wide_signed;
        return static_cast<W>(a.num) * b.den + static_cast<W>(b.num) * a.den <= static_cast<W>(a.den) * b.den;
    }

    // Combines two shares.  A sum above one has no share, so add refuses it.
    // The reduction happens in the wide type, before narrowing, for the reason
    // given at simplify_wide.  The fatal invariant refuses a sum whose reduced
    // denominator is above the bound, such as 1/(2^31 - 1) + 1/2^31.
    [[nodiscard]] static constexpr element_type add(element_type a, element_type b) noexcept {
        CRUCIBLE_PRE(a.is_well_formed() && b.is_well_formed());
        CRUCIBLE_PRE(is_sum_at_most_one(a, b));
        using W = Rational::wide_signed;
        const W num128 = static_cast<W>(a.num) * b.den + static_cast<W>(b.num) * a.den;
        const W den128 = static_cast<W>(a.den) * b.den;
        const Rational sum = simplify_wide(num128, den128);
        CRUCIBLE_FATAL_INVARIANT(sum.is_well_formed());
        return sum;
    }

    // Splits a share by a share.  Multiplying two bounded numerators, and two
    // bounded denominators, stays inside the narrow type, so this one narrows
    // before reducing.  The fatal invariant refuses a product whose reduced
    // denominator is above the bound, such as 1/2^31 * 1/2^31.
    [[nodiscard]] static constexpr element_type mul(element_type a, element_type b) noexcept {
        CRUCIBLE_PRE(a.is_well_formed() && b.is_well_formed());
        using W = Rational::wide_signed;
        const W num128 = static_cast<W>(a.num) * b.num;
        const W den128 = static_cast<W>(a.den) * b.den;
        const Rational product = simplify(Rational{
            static_cast<std::int64_t>(num128),
            static_cast<std::int64_t>(den128),
        });
        CRUCIBLE_FATAL_INVARIANT(product.is_well_formed());
        return product;
    }

    [[nodiscard]] static consteval std::string_view name() noexcept { return "FractionalLattice"; }
};

namespace detail {

// The carrier of a shared value.  A share stored beside a value goes
// through the order dual.  The check file of this header and
// test/foundation/test_lattices_core.cpp name it.
template <typename T>
using SharedPermissionGraded = Graded<ModalityKind::Absolute, DualLattice<FractionalLattice>, T>;

}  // namespace detail

}  // namespace foundation::algebra::lattices
