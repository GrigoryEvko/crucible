#pragma once

// Tropical min-plus semiring over the natural numbers with infinity,
// carrying a chain-lattice reading on the same carrier.  Staleness
// counts how many steps behind the current one a value is.  Zero is
// fresh and infinity means never observed.
//
// The two readings answer different questions and both are needed.
// Tropical addition is min and picks the freshest of competing
// estimates for one value.  Tropical multiplication is ordinary
// addition and accumulates a worst-case bound along a chain of
// operations.  The lattice join is max, so composing two graded values
// keeps the more pessimistic bound.
//
// Infinity is encoded as the largest representable value.  That keeps
// the element one word wide and makes the default comparison order the
// intended one.  The cost is that a finite staleness cannot use that
// value.  Saturating addition folds any overflow onto it, which is the
// right answer: a staleness past the representable range is unbounded
// for any practical purpose.

#include <crucible/Saturate.h>
#include <crucible/algebra/_Graded.h>
#include <crucible/algebra/_Lattice.h>

#include <algorithm>
#include <compare>
#include <cstdint>
#include <limits>
#include <string_view>
#include <type_traits>

namespace crucible::algebra::lattices {

struct StalenessSemiring {
    struct element_type {
        std::uint64_t value{0};

        [[nodiscard]] static constexpr element_type infinity() noexcept {
            return element_type{std::numeric_limits<std::uint64_t>::max()};
        }

        [[nodiscard]] constexpr bool is_infinite() const noexcept {
            return value == std::numeric_limits<std::uint64_t>::max();
        }
        [[nodiscard]] constexpr bool is_finite() const noexcept {
            return value != std::numeric_limits<std::uint64_t>::max();
        }

        [[nodiscard]] friend constexpr auto operator<=>(element_type, element_type) noexcept = default;
        [[nodiscard]] friend constexpr bool operator==(element_type, element_type) noexcept = default;
    };

    [[nodiscard]] static constexpr element_type bottom() noexcept { return element_type{0}; }
    [[nodiscard]] static constexpr element_type top() noexcept { return element_type::infinity(); }
    [[nodiscard]] static constexpr bool leq(element_type a, element_type b) noexcept { return a.value <= b.value; }
    [[nodiscard]] static constexpr element_type join(element_type a, element_type b) noexcept {
        return element_type{std::max(a.value, b.value)};
    }
    [[nodiscard]] static constexpr element_type meet(element_type a, element_type b) noexcept {
        return element_type{std::min(a.value, b.value)};
    }

    [[nodiscard]] static constexpr element_type zero() noexcept { return top(); }
    [[nodiscard]] static constexpr element_type one() noexcept { return bottom(); }
    [[nodiscard]] static constexpr element_type add(element_type a, element_type b) noexcept { return meet(a, b); }
    [[nodiscard]] static constexpr element_type mul(element_type a, element_type b) noexcept {
        if (a.is_infinite() || b.is_infinite()) {
            return top();
        }
        return element_type{::crucible::sat::add_sat(a.value, b.value)};
    }

    [[nodiscard]] static consteval std::string_view name() noexcept { return "StalenessSemiring"; }
};

namespace staleness {

inline constexpr StalenessSemiring::element_type fresh = StalenessSemiring::bottom();
inline constexpr StalenessSemiring::element_type infinite = StalenessSemiring::top();

[[nodiscard]] constexpr StalenessSemiring::element_type at(std::uint64_t n) noexcept
    pre(n < std::numeric_limits<std::uint64_t>::max()) {
    return StalenessSemiring::element_type{n};
}

}  // namespace staleness

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

template <typename T>
using StaleGraded = Graded<ModalityKind::Absolute, StalenessSemiring, T>;

static_assert(sizeof(StaleGraded<OneByteValue>) == sizeof(OneByteValue) + sizeof(StalenessSemiring::element_type) + 7);

static_assert(sizeof(StaleGraded<EightByteValue>) == sizeof(EightByteValue) + sizeof(StalenessSemiring::element_type));

// Static assertions alone can mask consteval, SFINAE and inline-body
// defects.  These calls pass non-constant arguments.
inline void runtime_smoke_test() {
    std::uint64_t n_a = 5;
    std::uint64_t n_b = 17;
    auto a = StalenessSemiring::element_type{n_a};
    auto b = StalenessSemiring::element_type{n_b};

    [[maybe_unused]] bool l = StalenessSemiring::leq(a, b);
    [[maybe_unused]] StalenessSemiring::element_type j = StalenessSemiring::join(a, b);
    [[maybe_unused]] StalenessSemiring::element_type m = StalenessSemiring::meet(a, b);

    [[maybe_unused]] auto sum = StalenessSemiring::add(a, b);
    [[maybe_unused]] auto prod = StalenessSemiring::mul(a, b);
    [[maybe_unused]] auto absb = StalenessSemiring::mul(a, StalenessSemiring::top());

    [[maybe_unused]] auto at_n = staleness::at(n_a);

    OneByteValue v{42};
    StaleGraded<OneByteValue> initial{v, StalenessSemiring::bottom()};
    auto widened = initial.weaken(a);
    auto widened2 = widened.weaken(b);
    auto composed = initial.compose(widened2);
    auto rv_widen = std::move(widened2).weaken(b);
    auto rv_comp = std::move(initial).compose(composed);

    [[maybe_unused]] auto g1 = composed.grade();
    [[maybe_unused]] auto v1 = composed.peek().c;
    [[maybe_unused]] auto v2 = std::move(rv_comp).consume().c;
    [[maybe_unused]] auto g2 = rv_widen.grade();
}

}  // namespace detail::staleness_semiring_self_test

}  // namespace crucible::algebra::lattices
