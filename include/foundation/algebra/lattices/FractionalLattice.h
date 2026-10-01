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

#include <foundation/algebra/Graded.h>
#include <foundation/algebra/Lattice.h>
#include <foundation/algebra/lattices/DualLattice.h>

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

    // Graded stores a share through the order dual (DualLattice.h).
    static constexpr ClaimOrientation claim_orientation = ClaimOrientation::stronger_is_higher;

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

namespace detail {

// The carrier of a shared value.  A share stored beside a value goes
// through the order dual.  The check file of this header and
// test/foundation/test_lattices_core.cpp name it.
template <typename T>
using SharedPermissionGraded = Graded<ModalityKind::Absolute, DualLattice<FractionalLattice>, T>;

}  // namespace detail

}  // namespace foundation::algebra::lattices
