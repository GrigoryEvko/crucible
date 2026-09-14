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

#include <crucible/Platform.h>
#include <crucible/algebra/Graded.h>
#include <crucible/algebra/Lattice.h>

#include <cmath>
#include <concepts>
#include <contracts>
#include <cstdint>
#include <functional>
#include <limits>
#include <string_view>
#include <type_traits>

namespace crucible::algebra::lattices {

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

namespace detail::monotone_lattice_self_test {

using MonU64Less = MonotoneLattice<std::uint64_t, std::less<std::uint64_t>>;
using MonI32Less = MonotoneLattice<std::int32_t, std::less<std::int32_t>>;
using MonF64Less = MonotoneLattice<double, std::less<double>>;

static_assert(Lattice<MonU64Less>);
static_assert(BoundedBelowLattice<MonU64Less>);
static_assert(BoundedAboveLattice<MonU64Less>);
static_assert(BoundedLattice<MonU64Less>);

static_assert(Lattice<MonI32Less>);
static_assert(BoundedLattice<MonI32Less>);

static_assert(Lattice<MonF64Less>);
static_assert(BoundedLattice<MonF64Less>);

using MonU64Greater = MonotoneLattice<std::uint64_t, std::greater<std::uint64_t>>;

static_assert(Lattice<MonU64Greater>);
static_assert(BoundedLattice<MonU64Greater>);

struct CustomLess {
    [[nodiscard]] constexpr bool operator()(int a, int b) const noexcept { return a < b; }
};
using MonI32Custom = MonotoneLattice<int, CustomLess>;

static_assert(Lattice<MonI32Custom>);
static_assert(!BoundedBelowLattice<MonI32Custom>);
static_assert(!BoundedAboveLattice<MonI32Custom>);
static_assert(!BoundedLattice<MonI32Custom>);
static_assert(UnboundedLattice<MonI32Custom>);

static_assert(MonU64Less::bottom() == 0u);
static_assert(MonU64Less::top() == std::numeric_limits<std::uint64_t>::max());
static_assert(MonI32Less::bottom() == std::numeric_limits<std::int32_t>::min());
static_assert(MonI32Less::top() == std::numeric_limits<std::int32_t>::max());

static_assert(MonU64Greater::bottom() == std::numeric_limits<std::uint64_t>::max());
static_assert(MonU64Greater::top() == 0u);

static_assert(MonU64Less::leq(0u, 1u));
static_assert(MonU64Less::leq(0u, 0u));
static_assert(!MonU64Less::leq(1u, 0u));
static_assert(MonU64Less::join(3u, 7u) == 7u);
static_assert(MonU64Less::meet(3u, 7u) == 3u);
static_assert(MonU64Less::join(3u, 3u) == 3u);
static_assert(MonU64Less::meet(3u, 3u) == 3u);

static_assert(MonU64Greater::leq(7u, 3u));
static_assert(!MonU64Greater::leq(3u, 7u));
static_assert(MonU64Greater::join(3u, 7u) == 3u);
static_assert(MonU64Greater::meet(3u, 7u) == 7u);

// The carrier is infinite, so the axioms are checked over a
// representative span of witnesses rather than exhaustively.
constexpr std::uint64_t u_bot = MonU64Less::bottom();
constexpr std::uint64_t u_lo = 1u;
constexpr std::uint64_t u_mid = std::uint64_t{1} << 32;
constexpr std::uint64_t u_hi = std::numeric_limits<std::uint64_t>::max() - 1u;
constexpr std::uint64_t u_top = MonU64Less::top();

static_assert(verify_bounded_lattice_axioms_at<MonU64Less>(u_bot, u_bot, u_bot));
static_assert(verify_bounded_lattice_axioms_at<MonU64Less>(u_bot, u_lo, u_top));
static_assert(verify_bounded_lattice_axioms_at<MonU64Less>(u_lo, u_mid, u_hi));
static_assert(verify_bounded_lattice_axioms_at<MonU64Less>(u_top, u_top, u_top));
static_assert(verify_bounded_lattice_axioms_at<MonU64Less>(u_hi, u_mid, u_lo));
static_assert(verify_bounded_lattice_axioms_at<MonU64Less>(u_top, u_bot, u_top));
static_assert(verify_bounded_lattice_axioms_at<MonU64Less>(u_bot, u_mid, u_top));

constexpr std::int32_t i_neg = -100;
constexpr std::int32_t i_zero = 0;
constexpr std::int32_t i_pos = 100;
constexpr std::int32_t i_bot = MonI32Less::bottom();
constexpr std::int32_t i_top = MonI32Less::top();

static_assert(verify_bounded_lattice_axioms_at<MonI32Less>(i_neg, i_zero, i_pos));
static_assert(verify_bounded_lattice_axioms_at<MonI32Less>(i_bot, i_zero, i_top));
static_assert(verify_bounded_lattice_axioms_at<MonI32Less>(i_top, i_neg, i_bot));

static_assert(verify_bounded_lattice_axioms_at<MonU64Greater>(u_bot, u_lo, u_top));
static_assert(verify_bounded_lattice_axioms_at<MonU64Greater>(u_top, u_mid, u_bot));

static_assert(MonU64Less::leq(0u, 100u));
static_assert(MonU64Less::leq(100u, 1000u));
static_assert(MonU64Less::leq(0u, 1000u));
static_assert(MonU64Less::join(100u, 1000u) == 1000u);
static_assert(MonU64Less::join(0u, MonU64Less::top()) == MonU64Less::top());

static_assert(MonU64Less::is_nan_safe(0u));
static_assert(MonU64Less::is_nan_safe(std::numeric_limits<std::uint64_t>::max()));
static_assert(MonF64Less::is_nan_safe(0.0));
static_assert(MonF64Less::is_nan_safe(1.0));
static_assert(MonF64Less::is_nan_safe(-1.0));
static_assert(MonF64Less::is_nan_safe(std::numeric_limits<double>::lowest()));
static_assert(MonF64Less::is_nan_safe(std::numeric_limits<double>::max()));
static_assert(MonF64Less::is_nan_safe(std::numeric_limits<double>::infinity()));
static_assert(MonF64Less::is_nan_safe(-std::numeric_limits<double>::infinity()));
static_assert(!MonF64Less::is_nan_safe(std::numeric_limits<double>::quiet_NaN()));
static_assert(!MonF64Less::is_nan_safe(std::numeric_limits<double>::signaling_NaN()));

static_assert(MonU64Less::name() == "MonotoneLattice");
static_assert(MonI32Custom::name() == "MonotoneLattice");

static_assert(std::is_same_v<MonU64Less::element_type, std::uint64_t>);
static_assert(std::is_same_v<MonU64Less::compare_type, std::less<std::uint64_t>>);
static_assert(std::is_same_v<MonU64Greater::compare_type, std::greater<std::uint64_t>>);

// The element type is the carrier itself, so the grade is not empty and
// cannot collapse by empty-base optimization.  Graded instead ships a
// partial specialization for the case where the value type equals the
// lattice element type, which stores one cell for both views.  The
// sizeof assertions below are the witness that it is still selected.
static_assert(!std::is_empty_v<MonU64Less::element_type>);
static_assert(sizeof(MonU64Less::element_type) == 8);

template <typename T>
using MonotonicGraded = Graded<ModalityKind::Absolute, MonotoneLattice<T, std::less<T>>, T>;

static_assert(sizeof(MonotonicGraded<std::uint64_t>) == sizeof(std::uint64_t));
static_assert(sizeof(MonotonicGraded<std::int32_t>) == sizeof(std::int32_t));

// Static assertions alone can mask consteval, SFINAE and inline-body
// defects.  These calls pass non-constant arguments.
inline void runtime_smoke_test() {
    std::uint64_t a = 100;
    std::uint64_t b = 1000;

    [[maybe_unused]] bool l = MonU64Less::leq(a, b);
    [[maybe_unused]] std::uint64_t j = MonU64Less::join(a, b);
    [[maybe_unused]] std::uint64_t m = MonU64Less::meet(a, b);

    [[maybe_unused]] std::uint64_t bot = MonU64Less::bottom();
    [[maybe_unused]] std::uint64_t top = MonU64Less::top();

    [[maybe_unused]] bool gl = MonU64Greater::leq(b, a);
    [[maybe_unused]] std::uint64_t gj = MonU64Greater::join(a, b);

    // The single-argument constructor sets value and grade together.
    // The two-argument form asserts that its arguments are already
    // lattice-equivalent, which this collapsed shape always makes true.
    MonotonicGraded<std::uint64_t> initial{a};
    auto widened = initial.weaken(a);
    auto widened2 = widened.weaken(b);
    auto composed = initial.compose(widened2);
    auto rv_widen = std::move(widened2).weaken(b);
    auto rv_comp = std::move(initial).compose(composed);

    [[maybe_unused]] auto g1 = composed.grade();
    [[maybe_unused]] auto v1 = composed.peek();
    [[maybe_unused]] auto v2 = std::move(rv_comp).consume();
    [[maybe_unused]] auto g2 = rv_widen.grade();

    double fa = -1.0;
    double fb = 0.0;
    double fc = std::numeric_limits<double>::infinity();
    [[maybe_unused]] bool fl1 = MonF64Less::leq(fa, fb);
    [[maybe_unused]] double fj1 = MonF64Less::join(fa, fc);
    [[maybe_unused]] double fm1 = MonF64Less::meet(fa, fc);
    [[maybe_unused]] bool fnan_a = MonF64Less::is_nan_safe(fa);
    [[maybe_unused]] bool fnan_b = MonF64Less::is_nan_safe(fb);
    [[maybe_unused]] bool fnan_inf = MonF64Less::is_nan_safe(fc);

    // This value never reaches leq, join or meet.  Passing it would
    // trip the invariant and abort the smoke test.
    double fnan = std::nan("");
    [[maybe_unused]] bool fnan_fired = !MonF64Less::is_nan_safe(fnan);
}

}  // namespace detail::monotone_lattice_self_test

}  // namespace crucible::algebra::lattices
