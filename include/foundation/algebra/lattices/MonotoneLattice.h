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
// Nothing here enforces that at compile time.  The bounds themselves
// are safe because lowest() and max() are finite.
//
// The lattice states no claim orientation.  A monotone count can be a
// lower bound or an upper bound, and the two bounds run in opposite
// directions.  Monotonic grades a value by the value itself, and Graded
// asks no orientation of a grade that is the value.  A stored grade in
// this order is refused (ClaimOrientation.h).

#include <foundation/Platform.h>
#include <foundation/algebra/Graded.h>
#include <foundation/algebra/Lattice.h>

#include <cmath>
#include <concepts>
#include <contracts>
#include <cstdint>
#include <functional>
#include <limits>
#include <string_view>
#include <type_traits>

namespace foundation::algebra::lattices {

// The primary template is undefined so an unbounded carrier fails to
// instantiate rather than inventing a bound.  The members are named
// lattice_bottom and lattice_top, not bottom and top, to keep them out
// of ADL against the lattice's own bottom() and top().
template <typename T, typename Cmp>
struct monotone_bounds;

template <typename T>
    requires std::is_arithmetic_v<T>
struct monotone_bounds<T, std::less<T>> {
    [[nodiscard]] static constexpr T lattice_bottom() noexcept { return std::numeric_limits<T>::lowest(); }
    [[nodiscard]] static constexpr T lattice_top() noexcept { return std::numeric_limits<T>::max(); }
};

template <typename T>
    requires std::is_arithmetic_v<T>
struct monotone_bounds<T, std::greater<T>> {
    [[nodiscard]] static constexpr T lattice_bottom() noexcept { return std::numeric_limits<T>::max(); }
    [[nodiscard]] static constexpr T lattice_top() noexcept { return std::numeric_limits<T>::lowest(); }
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
    // It is fatal rather than debug-only because keeping NaN out is a rule
    // for callers, which the header comment above says nothing here
    // enforces, so it is a claim and not a fact.  A plain invariant states
    // it to the optimizer under NDEBUG, and an ordered compare on a value
    // that is in fact NaN then becomes undefined in exactly the build that
    // ships.  For every carrier that is not floating point is_nan_safe is
    // constant true, so the branch folds away and only the floating-point
    // instantiations pay for it.
    [[nodiscard]] static constexpr bool is_nan_safe(T const& x) noexcept {
        if constexpr (std::is_floating_point_v<T>) {
            return !std::isnan(x);
        } else {
            return true;
        }
    }

    [[nodiscard]] static constexpr bool leq(T const& a, T const& b) noexcept {
        CRUCIBLE_FATAL_INVARIANT(is_nan_safe(a) && is_nan_safe(b));
        return !Cmp{}(b, a);
    }
    [[nodiscard]] static constexpr T join(T const& a, T const& b) noexcept {
        CRUCIBLE_FATAL_INVARIANT(is_nan_safe(a) && is_nan_safe(b));
        return Cmp{}(a, b) ? b : a;
    }
    [[nodiscard]] static constexpr T meet(T const& a, T const& b) noexcept {
        CRUCIBLE_FATAL_INVARIANT(is_nan_safe(a) && is_nan_safe(b));
        return Cmp{}(a, b) ? a : b;
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

    [[nodiscard]] static consteval std::string_view name() noexcept { return "MonotoneLattice"; }
};

namespace detail {

// The orders and the carrier of the MonotoneLattice checks.  The check
// file of this header and test/foundation/test_lattices_core.cpp name
// them, so they live here and not in the check file.
using MonU64Less = MonotoneLattice<std::uint64_t, std::less<std::uint64_t>>;
using MonF64Less = MonotoneLattice<double, std::less<double>>;
using MonU64Greater = MonotoneLattice<std::uint64_t, std::greater<std::uint64_t>>;

template <typename T>
using MonotonicGraded = Graded<ModalityKind::Absolute, MonotoneLattice<T, std::less<T>>, T>;

}  // namespace detail

}  // namespace foundation::algebra::lattices
