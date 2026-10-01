// The compile-time checks of foundation/algebra/lattices/StalenessSemiring.h.

#include <foundation/algebra/lattices/StalenessSemiring.h>

namespace foundation::algebra::lattices {

namespace detail::staleness_semiring_self_test {

static_assert(Lattice<StalenessSemiring>);
static_assert(BoundedBelowLattice<StalenessSemiring>);
static_assert(BoundedAboveLattice<StalenessSemiring>);
static_assert(BoundedLattice<StalenessSemiring>);
static_assert(Semiring<StalenessSemiring>);

static_assert(sizeof(StalenessSemiring::element_type) == sizeof(std::uint64_t));
static_assert(alignof(StalenessSemiring::element_type) == alignof(std::uint64_t));
static_assert(!std::is_empty_v<StalenessSemiring::element_type>);

static_assert(StalenessSemiring::element_type{} == StalenessSemiring::bottom());

static_assert(StalenessSemiring::top().is_infinite());
static_assert(!StalenessSemiring::bottom().is_infinite());
static_assert(StalenessSemiring::element_type{42}.is_infinite() == false);
static_assert(StalenessSemiring::element_type{std::numeric_limits<std::uint64_t>::max()}.is_infinite());

static_assert(StalenessSemiring::bottom().is_finite());
static_assert(!StalenessSemiring::top().is_finite());
static_assert(StalenessSemiring::element_type{42}.is_finite());
static_assert(!StalenessSemiring::element_type{std::numeric_limits<std::uint64_t>::max()}.is_finite());

constexpr auto fresh = StalenessSemiring::bottom();
constexpr auto stale1 = StalenessSemiring::element_type{1};
constexpr auto stale100 = StalenessSemiring::element_type{100};
constexpr auto stale1k = StalenessSemiring::element_type{1000};
constexpr auto inf = StalenessSemiring::top();

static_assert(StalenessSemiring::leq(fresh, stale1));
static_assert(StalenessSemiring::leq(stale1, stale100));
static_assert(StalenessSemiring::leq(stale100, stale1k));
static_assert(StalenessSemiring::leq(stale1k, inf));
static_assert(StalenessSemiring::leq(fresh, inf));
static_assert(StalenessSemiring::leq(fresh, fresh));
static_assert(StalenessSemiring::leq(inf, inf));
static_assert(!StalenessSemiring::leq(stale1, fresh));
static_assert(!StalenessSemiring::leq(inf, stale1k));

static_assert(StalenessSemiring::join(stale1, stale100) == stale100);
static_assert(StalenessSemiring::join(stale100, inf) == inf);
static_assert(StalenessSemiring::meet(stale1, stale100) == stale1);
static_assert(StalenessSemiring::meet(fresh, stale100) == fresh);
static_assert(StalenessSemiring::meet(stale1k, inf) == stale1k);

static_assert(StalenessSemiring::add(stale1, stale100) == stale1);
static_assert(StalenessSemiring::add(stale1k, inf) == stale1k);
static_assert(StalenessSemiring::add(StalenessSemiring::zero(), stale1) == stale1);

static_assert(StalenessSemiring::mul(stale1, stale100) == StalenessSemiring::element_type{101});
static_assert(StalenessSemiring::mul(StalenessSemiring::one(), stale1) == stale1);
static_assert(StalenessSemiring::mul(StalenessSemiring::one(), inf) == inf);
static_assert(StalenessSemiring::mul(inf, stale1) == inf);
static_assert(StalenessSemiring::mul(stale1, inf) == inf);

constexpr auto near_max = StalenessSemiring::element_type{std::numeric_limits<std::uint64_t>::max() - 5};
static_assert(StalenessSemiring::mul(near_max, StalenessSemiring::element_type{10}) == inf);

// The carrier is infinite, so the axioms are checked over a
// representative span of witnesses rather than exhaustively.
static_assert(verify_bounded_lattice_axioms_at<StalenessSemiring>(fresh, fresh, fresh));
static_assert(verify_bounded_lattice_axioms_at<StalenessSemiring>(fresh, stale1, inf));
static_assert(verify_bounded_lattice_axioms_at<StalenessSemiring>(stale1, stale100, stale1k));
static_assert(verify_bounded_lattice_axioms_at<StalenessSemiring>(stale1k, stale100, stale1));
static_assert(verify_bounded_lattice_axioms_at<StalenessSemiring>(inf, inf, inf));
static_assert(verify_bounded_lattice_axioms_at<StalenessSemiring>(inf, stale1, fresh));
static_assert(verify_bounded_lattice_axioms_at<StalenessSemiring>(fresh, stale100, inf));

static_assert(verify_semiring_axioms_at<StalenessSemiring>(fresh, fresh, fresh));
static_assert(verify_semiring_axioms_at<StalenessSemiring>(fresh, stale1, stale100));
static_assert(verify_semiring_axioms_at<StalenessSemiring>(stale1, stale100, stale1k));
static_assert(verify_semiring_axioms_at<StalenessSemiring>(stale1k, stale100, stale1));
static_assert(verify_semiring_axioms_at<StalenessSemiring>(inf, inf, inf));
static_assert(verify_semiring_axioms_at<StalenessSemiring>(inf, stale1, fresh));
static_assert(verify_semiring_axioms_at<StalenessSemiring>(stale1, inf, stale100));

static_assert(StalenessSemiring::name() == "StalenessSemiring");

static_assert(staleness::fresh == StalenessSemiring::bottom());
static_assert(staleness::infinite == StalenessSemiring::top());
static_assert(staleness::at(42) == StalenessSemiring::element_type{42});

// Distributivity survives saturation because saturating addition is
// monotone in both arguments and min is the meet of a total order.  A
// monotone f satisfies f(min(x, y)) == min(f(x), f(y)), and tropical
// distributivity is that identity at f(x) = sat_add(a, x).  The
// witnesses below cluster at the saturation boundary, so an addition
// that wrapped instead of clamping would fail the check.
[[nodiscard]] consteval bool exhaustive_saturation_axioms() noexcept {
    constexpr StalenessSemiring::element_type witnesses[] = {
        StalenessSemiring::element_type{0},
        StalenessSemiring::element_type{1},
        StalenessSemiring::element_type{5},
        StalenessSemiring::element_type{100},
        StalenessSemiring::element_type{1000},
        StalenessSemiring::element_type{std::numeric_limits<std::uint64_t>::max() - 100},
        StalenessSemiring::element_type{std::numeric_limits<std::uint64_t>::max() - 10},
        StalenessSemiring::element_type{std::numeric_limits<std::uint64_t>::max() - 5},
        StalenessSemiring::element_type{std::numeric_limits<std::uint64_t>::max() - 2},
        StalenessSemiring::element_type{std::numeric_limits<std::uint64_t>::max() - 1},
        StalenessSemiring::top(),
    };
    for (auto a : witnesses) {
        for (auto b : witnesses) {
            for (auto c : witnesses) {
                if (!verify_semiring_axioms_at<StalenessSemiring>(a, b, c)) {
                    return false;
                }
            }
        }
    }
    return true;
}
static_assert(exhaustive_saturation_axioms(), "StalenessSemiring tropical semiring axioms fail at some triple in "
                                              "the saturation boundary region.  The saturating add no longer "
                                              "commutes with min, which breaks distributivity there.");

struct OneByteValue {
    char c{0};
};
struct EightByteValue {
    unsigned long long v{0};
};

static_assert(sizeof(StaleGraded<OneByteValue>) == sizeof(OneByteValue) + sizeof(StalenessSemiring::element_type) + 7);

static_assert(sizeof(StaleGraded<EightByteValue>) == sizeof(EightByteValue) + sizeof(StalenessSemiring::element_type));

}  // namespace detail::staleness_semiring_self_test

}  // namespace foundation::algebra::lattices
