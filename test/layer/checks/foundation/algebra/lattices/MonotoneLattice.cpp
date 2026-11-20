// The compile-time checks of foundation/algebra/lattices/MonotoneLattice.h.

#include <foundation/algebra/lattices/MonotoneLattice.h>
#include <foundation/core/Number.h>
#include <foundation/core/Scalar.h>

#include <cstdint>

namespace foundation::algebra::lattices {

namespace detail::monotone_lattice_self_test {

using MonI32Less = MonotoneLattice<std::int32_t, std::less<std::int32_t>>;

static_assert(Lattice<MonU64Less>);
static_assert(BoundedBelowLattice<MonU64Less>);
static_assert(BoundedAboveLattice<MonU64Less>);
static_assert(BoundedLattice<MonU64Less>);

static_assert(Lattice<MonI32Less>);
static_assert(BoundedLattice<MonI32Less>);

static_assert(Lattice<MonF64Less>);
static_assert(BoundedLattice<MonF64Less>);

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
static_assert(MonU64Less::top() == ::foundation::core::max_of<std::uint64_t>);
static_assert(MonI32Less::bottom() == ::foundation::core::min_of<std::int32_t>);
static_assert(MonI32Less::top() == ::foundation::core::max_of<std::int32_t>);

static_assert(MonU64Greater::bottom() == ::foundation::core::max_of<std::uint64_t>);
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
constexpr std::uint64_t u_hi = ::foundation::core::max_of<std::uint64_t> - 1u;
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
static_assert(MonU64Less::is_nan_safe(::foundation::core::max_of<std::uint64_t>));
static_assert(MonF64Less::is_nan_safe(0.0));
static_assert(MonF64Less::is_nan_safe(1.0));
static_assert(MonF64Less::is_nan_safe(-1.0));
static_assert(MonF64Less::is_nan_safe(::foundation::core::min_of<double>));
static_assert(MonF64Less::is_nan_safe(::foundation::core::max_of<double>));
static_assert(MonF64Less::is_nan_safe(::foundation::core::infinity_of<double>));
static_assert(MonF64Less::is_nan_safe(-::foundation::core::infinity_of<double>));
static_assert(!MonF64Less::is_nan_safe(::foundation::core::quiet_nan_of<double>));
static_assert(!MonF64Less::is_nan_safe(::foundation::core::signaling_nan_of<double>));

// The bounds of a floating-point order are the two infinities, so each
// value other than NaN is inside the order.  -0 is below +0 under
// std::less, and above it under std::greater, so join and meet give the
// same bits for the two orders of their operands.
[[nodiscard]] consteval std::uint64_t bits_of(double value) noexcept {
    return ::foundation::core::bit_cast<std::uint64_t>(value);
}

constexpr double kInfinity = ::foundation::core::infinity_of<double>;

static_assert(bits_of(MonF64Less::bottom()) == bits_of(-kInfinity));
static_assert(bits_of(MonF64Less::top()) == bits_of(kInfinity));
static_assert(bits_of(MonF64Greater::bottom()) == bits_of(kInfinity));
static_assert(bits_of(MonF64Greater::top()) == bits_of(-kInfinity));
static_assert(MonF64Less::leq(-0.0, 0.0) && !MonF64Less::leq(0.0, -0.0));
static_assert(MonF64Greater::leq(0.0, -0.0) && !MonF64Greater::leq(-0.0, 0.0));
static_assert(bits_of(MonF64Less::join(-0.0, 0.0)) == bits_of(0.0)
              && bits_of(MonF64Less::join(0.0, -0.0)) == bits_of(0.0));
static_assert(bits_of(MonF64Less::meet(-0.0, 0.0)) == bits_of(-0.0)
              && bits_of(MonF64Less::meet(0.0, -0.0)) == bits_of(-0.0));
static_assert(bits_of(MonF64Greater::join(-0.0, 0.0)) == bits_of(-0.0)
              && bits_of(MonF64Greater::join(0.0, -0.0)) == bits_of(-0.0));

// The laws of an order over double, bit for bit, at a triple of values:
// each value is inside the order, join and meet are idempotent,
// commutative, associative and absorbing, and leq is antisymmetric.
template <typename L>
[[nodiscard]] consteval bool float_laws_hold_at(double a, double b, double c) {
    const bool is_inside = L::leq(L::bottom(), a) && L::leq(a, L::top());
    const bool is_idempotent = bits_of(L::join(a, a)) == bits_of(a) && bits_of(L::meet(a, a)) == bits_of(a);
    const bool is_commutative =
        bits_of(L::join(a, b)) == bits_of(L::join(b, a)) && bits_of(L::meet(a, b)) == bits_of(L::meet(b, a));
    const bool is_associative = bits_of(L::join(L::join(a, b), c)) == bits_of(L::join(a, L::join(b, c)))
                             && bits_of(L::meet(L::meet(a, b), c)) == bits_of(L::meet(a, L::meet(b, c)));
    const bool is_absorbing =
        bits_of(L::join(a, L::meet(a, b))) == bits_of(a) && bits_of(L::meet(a, L::join(a, b))) == bits_of(a);
    const bool is_antisymmetric = !(L::leq(a, b) && L::leq(b, a)) || bits_of(a) == bits_of(b);
    return is_inside && is_idempotent && is_commutative && is_associative && is_absorbing && is_antisymmetric;
}

template <typename L, typename... Values>
[[nodiscard]] consteval bool float_laws_hold_for_pair(double a, double b, Values... values) {
    return (float_laws_hold_at<L>(a, b, values) && ...);
}

template <typename L, typename... Values>
[[nodiscard]] consteval bool float_laws_hold_for_first(double a, Values... values) {
    return (float_laws_hold_for_pair<L>(a, values, values...) && ...);
}

// Each triple of the values.  Complexity: cubic in the values.
template <typename L, typename... Values>
[[nodiscard]] consteval bool float_laws_hold(Values... values) {
    return (float_laws_hold_for_first<L>(values, values...) && ...);
}

// The special values of double: the infinities, the limits, the two zeros,
// the least subnormals and the least normals.
template <typename L>
[[nodiscard]] consteval bool float_laws_hold_at_the_special_values() {
    constexpr double kHighest = ::foundation::core::max_of<double>;
    constexpr double kNormal = ::foundation::core::min_normal_of<double>;
    constexpr double kSubnormal = ::foundation::core::min_subnormal_of<double>;
    return float_laws_hold<L>(-kInfinity, -kHighest, -1.0, -kNormal, -kSubnormal, -0.0, 0.0, kSubnormal, kNormal, 1.0,
                              kHighest, kInfinity);
}

static_assert(float_laws_hold_at_the_special_values<MonF64Less>());
static_assert(float_laws_hold_at_the_special_values<MonF64Greater>());

static_assert(MonU64Less::name() == "MonotoneLattice");
static_assert(MonI32Custom::name() == "MonotoneLattice");

static_assert(std::is_same_v<MonU64Less::element_type, std::uint64_t>);
static_assert(std::is_same_v<MonU64Less::compare_type, std::less<std::uint64_t>>);
static_assert(std::is_same_v<MonU64Greater::compare_type, std::greater<std::uint64_t>>);

// The element type is the carrier itself, so the grade is not empty and
// cannot collapse by empty-base optimization.  Where the value type is
// the lattice element type, Graded stores one member for both views.
// The sizeof assertions below are the witness that it still does.
static_assert(!std::is_empty_v<MonU64Less::element_type>);
static_assert(sizeof(MonU64Less::element_type) == 8);

static_assert(sizeof(MonotonicGraded<std::uint64_t>) == sizeof(std::uint64_t));
static_assert(sizeof(MonotonicGraded<std::int32_t>) == sizeof(std::int32_t));

static_assert(claim_orientation_t<MonU64Less>::value == ClaimOrientation::unstated);
static_assert(LatticeGradesValue<MonU64Less, std::uint64_t> && !LatticeGradesValue<MonU64Less, std::int32_t>,
              "the order grades a value that is its own grade, and no value beside it");

}  // namespace detail::monotone_lattice_self_test

}  // namespace foundation::algebra::lattices
