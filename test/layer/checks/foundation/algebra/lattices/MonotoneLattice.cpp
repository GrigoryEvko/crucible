// The compile-time checks of foundation/algebra/lattices/MonotoneLattice.h.

#include <foundation/algebra/lattices/MonotoneLattice.h>

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
// cannot collapse by empty-base optimization.  Where the value type is
// the lattice element type, Graded stores one member for both views.
// The sizeof assertions below are the witness that it still does.
static_assert(!std::is_empty_v<MonU64Less::element_type>);
static_assert(sizeof(MonU64Less::element_type) == 8);

static_assert(sizeof(MonotonicGraded<std::uint64_t>) == sizeof(std::uint64_t));
static_assert(sizeof(MonotonicGraded<std::int32_t>) == sizeof(std::int32_t));

static_assert(claim_orientation_v<MonU64Less> == ClaimOrientation::unstated);
static_assert(LatticeGradesValue<MonU64Less, std::uint64_t> && !LatticeGradesValue<MonU64Less, std::int32_t>,
              "the order grades a value that is its own grade, and no value beside it");

}  // namespace detail::monotone_lattice_self_test

}  // namespace foundation::algebra::lattices
