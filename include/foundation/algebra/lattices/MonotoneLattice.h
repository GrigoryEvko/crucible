#pragma once

// Chain lattice over an arbitrary comparison functor.  Cmp{}(a, b) must
// mean "a is strictly less than b".  leq negates the strict inverse,
// join takes the max under Cmp and meet takes the min.  A non-strict
// comparison breaks antisymmetry and leaves leq true everywhere.
//
// bottom and top exist only where monotone_bounds is specialized.  For
// an arbitrary carrier and comparison there may be no smallest or
// largest element, so an unconditional bound would force every
// instantiation to invent one.  Unspecialized pairs stay Lattice-only.
//
// NaN breaks the laws for a floating-point carrier.  Every comparison
// involving NaN is false, so join stops being commutative: join(NaN,
// 1.0) is NaN while join(1.0, NaN) is 1.0.  Callers must keep NaN out.
// Nothing here enforces that at compile time.
//
// The other special values are in the carrier.  The bounds of a
// floating-point type with infinities are the two infinities, so each
// value other than NaN is inside the order.  The two zeros are equal under
// Cmp, but their bits differ, so the order puts -0 below +0 under
// std::less, and the dual order puts +0 below -0 under std::greater.  Then
// leq is antisymmetric bit for bit, and join and meet are commutative bit
// for bit.
//
// The lattice states no claim orientation.  A monotone count can be a
// lower bound or an upper bound, and the two bounds run in opposite
// directions.  Monotonic grades a value by the value itself, and Graded
// asks no orientation of a grade that is the value.  A stored grade in
// this order is refused (ClaimOrientation.h).

#include <foundation/Platform.h>
#include <foundation/algebra/Graded.h>
#include <foundation/algebra/Lattice.h>
#include <foundation/core/Text.h>

#include <concepts>
#include <cstdint>
#include <functional>
#include <limits>
#include <type_traits>

namespace foundation::algebra::lattices {

// The primary template is undefined so an unbounded carrier fails to
// instantiate rather than inventing a bound.  The members are named
// lattice_bottom and lattice_top, not bottom and top, to keep them out
// of ADL against the lattice's own bottom() and top().
template <typename T, typename Cmp>
struct monotone_bounds;

namespace detail {

// The greatest value of an arithmetic type: the infinity of a
// floating-point type, and max() of each other type.  The builtins give the
// infinity of each floating-point type.
template <typename T>
[[nodiscard]] constexpr T greatest_value_of() noexcept {
    if constexpr (std::is_same_v<T, float>) {
        return __builtin_inff();
    } else if constexpr (std::is_same_v<T, double>) {
        return __builtin_inf();
    } else if constexpr (std::is_same_v<T, long double>) {
        return __builtin_infl();
    } else if constexpr (std::is_floating_point_v<T>) {
        return static_cast<T>(__builtin_infl());
    } else {
        return std::numeric_limits<T>::max();
    }
}

// The least value of an arithmetic type: the negative infinity of a
// floating-point type, and lowest() of each other type.
template <typename T>
[[nodiscard]] constexpr T least_value_of() noexcept {
    if constexpr (std::is_floating_point_v<T>) {
        return -greatest_value_of<T>();
    } else {
        return std::numeric_limits<T>::lowest();
    }
}

}  // namespace detail

template <typename T>
    requires std::is_arithmetic_v<T>
struct monotone_bounds<T, std::less<T>> {
    [[nodiscard]] static constexpr T lattice_bottom() noexcept { return detail::least_value_of<T>(); }
    [[nodiscard]] static constexpr T lattice_top() noexcept { return detail::greatest_value_of<T>(); }
};

template <typename T>
    requires std::is_arithmetic_v<T>
struct monotone_bounds<T, std::greater<T>> {
    [[nodiscard]] static constexpr T lattice_bottom() noexcept { return detail::greatest_value_of<T>(); }
    [[nodiscard]] static constexpr T lattice_top() noexcept { return detail::least_value_of<T>(); }
};

template <typename T, typename Cmp>
concept HasMonotoneBounds = requires {
    { monotone_bounds<T, Cmp>::lattice_bottom() } -> std::same_as<T>;
    { monotone_bounds<T, Cmp>::lattice_top() } -> std::same_as<T>;
};

template <typename T, typename Cmp = std::less<T>>
struct MonotoneLattice {
    using element_type = T;
    using compare_type = Cmp;

    // The guard is a fatal invariant rather than a contract assertion so
    // that it fires independently of the including translation unit's
    // contract-evaluation semantic.  A hot-path unit that ignores
    // contracts would otherwise drop the NaN check silently.
    //
    // Keeping NaN out is a rule for callers, which the header comment above
    // says nothing here enforces, so it is a claim and not a fact.  An
    // assumption of it would make an ordered compare on a value that is in
    // fact NaN undefined in exactly the build that ships.  For every
    // carrier that is not floating point is_nan_safe is constant true, so
    // the branch folds away and only the floating-point instantiations pay
    // for it.
    [[nodiscard]] static constexpr bool is_nan_safe(T const& x) noexcept {
        if constexpr (std::is_floating_point_v<T>) {
            return !__builtin_isnan(x);
        } else {
            return true;
        }
    }

    // True when a is strictly before b: Cmp, with the two zeros of a
    // floating-point type in an order.  Two values other than NaN that Cmp
    // ties are equal, and only the two zeros are equal with different bits.
    // Cmp orders them as it orders -1 and +1, so the dual order reverses the
    // zeros too.  For a carrier that is not floating point, the tie arm is
    // gone and this is Cmp.
    [[nodiscard]] static constexpr bool is_before(T const& a, T const& b) noexcept {
        if (Cmp{}(a, b)) return true;
        if constexpr (std::is_floating_point_v<T>) {
            const bool is_a_negative = __builtin_signbit(a) != 0;
            const bool is_b_negative = __builtin_signbit(b) != 0;
            if (is_a_negative != is_b_negative && !Cmp{}(b, a)) {
                return Cmp{}(is_a_negative ? T{-1} : T{1}, is_b_negative ? T{-1} : T{1});
            }
        }
        return false;
    }

    [[nodiscard]] static constexpr bool leq(T const& a, T const& b) noexcept {
        CRUCIBLE_FATAL_INVARIANT(is_nan_safe(a) && is_nan_safe(b));
        return !is_before(b, a);
    }
    [[nodiscard]] static constexpr T join(T const& a, T const& b) noexcept {
        CRUCIBLE_FATAL_INVARIANT(is_nan_safe(a) && is_nan_safe(b));
        return is_before(a, b) ? b : a;
    }
    [[nodiscard]] static constexpr T meet(T const& a, T const& b) noexcept {
        CRUCIBLE_FATAL_INVARIANT(is_nan_safe(a) && is_nan_safe(b));
        return is_before(a, b) ? a : b;
    }

    [[nodiscard]] static constexpr T bottom() noexcept
        requires HasMonotoneBounds<T, Cmp>
    {
        return monotone_bounds<T, Cmp>::lattice_bottom();
    }
    [[nodiscard]] static constexpr T top() noexcept
        requires HasMonotoneBounds<T, Cmp>
    {
        return monotone_bounds<T, Cmp>::lattice_top();
    }

    [[nodiscard]] static consteval ::foundation::core::TextView name() noexcept { return "MonotoneLattice"; }
};

namespace detail {

// The orders and the carrier of the MonotoneLattice checks.  The check
// file of this header and test/foundation/test_lattices_core.cpp name
// them, so they live here and not in the check file.
using MonU64Less = MonotoneLattice<std::uint64_t, std::less<std::uint64_t>>;
using MonF64Less = MonotoneLattice<double, std::less<double>>;
using MonU64Greater = MonotoneLattice<std::uint64_t, std::greater<std::uint64_t>>;
using MonF64Greater = MonotoneLattice<double, std::greater<double>>;

template <typename T>
using MonotonicGraded = Graded<ModalityKind::Absolute, MonotoneLattice<T, std::less<T>>, T>;

}  // namespace detail

}  // namespace foundation::algebra::lattices
