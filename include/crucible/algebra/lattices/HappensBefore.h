#pragma once

// The vector clock of Lamport 1978, Mattern 1988 and Fidge 1991, presented as
// the lattice of N-tuples of naturals under the pointwise order.

#include <crucible/algebra/_Lattice.h>
#include <crucible/Platform.h>
#include <crucible/safety/_Decide.h>

#include <array>
#include <compare>
#include <cstdint>
#include <cstddef>
#include <limits>
#include <string_view>
#include <utility>

namespace crucible::algebra::lattices {

// Tag has no members and no effect on layout.  It exists so that clocks
// belonging to different protocols are different types and cannot be joined
// with one another by accident.
template <std::size_t N, typename Tag = void>
struct HappensBeforeLattice {
    static_assert(N > 0, "HappensBeforeLattice<0> is forbidden — an empty vector clock "
                         "has no algebraic content.  Use N >= 1; N=1 reduces to a "
                         "Lamport scalar clock.");

    // operator<=> is written out rather than defaulted.  A defaulted one would
    // forward to the array member and yield a lexicographic strong_ordering,
    // which is the wrong relation: two clocks that each lead on a different
    // slot are unordered, not one-before-the-other.  A non-strong ordering
    // also supplies no operator== of its own, so that one stays defaulted.
    struct element_type {
        std::array<std::uint64_t, N> clock{};

        [[nodiscard]] constexpr bool operator==(element_type const&) const noexcept = default;

        [[nodiscard]] constexpr std::partial_ordering operator<=>(element_type const& other) const noexcept {
            bool self_leq_other = true;
            bool other_leq_self = true;
            for (std::size_t i = 0; i < N; ++i) {
                if (clock[i] > other.clock[i]) self_leq_other = false;
                if (other.clock[i] > clock[i]) other_leq_self = false;
                if (!self_leq_other && !other_leq_self) break;
            }
            if (self_leq_other && other_leq_self) return std::partial_ordering::equivalent;
            if (self_leq_other) return std::partial_ordering::less;
            if (other_leq_self) return std::partial_ordering::greater;
            return std::partial_ordering::unordered;
        }

        // Read-only by design.  A clock advances only through successor_at and
        // causal_merge, so no accessor hands out a mutable slot.
        //
        // The bound is spelled `N - 1` and cannot underflow, because a zero N
        // is a hard build error above.
        [[nodiscard]] constexpr std::uint64_t operator[](std::size_t p) const noexcept
            pre(::crucible::decide::in_range<std::size_t>(p, 0, N - 1)) {
            return clock[p];
        }
    };

    static constexpr std::size_t process_count = N;
    using process_id_type = std::size_t;
    using clock_value_type = std::uint64_t;
    using tag_type = Tag;

    [[nodiscard]] static constexpr element_type bottom() noexcept { return element_type{}; }

    // The order over the naturals has no greatest element, so this top is
    // synthesized rather than real.  A bounded lattice needs some witness, and
    // a clock that reached the maximum would already have overflowed.
    [[nodiscard]] static constexpr element_type top() noexcept {
        element_type result;
        for (std::size_t i = 0; i < N; ++i) {
            result.clock[i] = std::numeric_limits<std::uint64_t>::max();
        }
        return result;
    }

    [[nodiscard]] static constexpr bool leq(element_type a, element_type b) noexcept {
        for (std::size_t i = 0; i < N; ++i) {
            if (a.clock[i] > b.clock[i]) return false;
        }
        return true;
    }

    [[nodiscard]] static constexpr element_type join(element_type a, element_type b) noexcept {
        element_type r;
        for (std::size_t i = 0; i < N; ++i) {
            r.clock[i] = a.clock[i] >= b.clock[i] ? a.clock[i] : b.clock[i];
        }
        return r;
    }

    [[nodiscard]] static constexpr element_type meet(element_type a, element_type b) noexcept {
        element_type r;
        for (std::size_t i = 0; i < N; ++i) {
            r.clock[i] = a.clock[i] <= b.clock[i] ? a.clock[i] : b.clock[i];
        }
        return r;
    }

    [[nodiscard]] static constexpr bool happens_before(element_type a, element_type b) noexcept {
        return leq(a, b) && !(a == b);
    }

    // Two clocks are concurrent when neither precedes the other.  This is what
    // a vector clock adds over a scalar one, whose order is total.  At N of one
    // the answer is always false.
    [[nodiscard]] static constexpr bool is_concurrent(element_type a, element_type b) noexcept {
        return !leq(a, b) && !leq(b, a);
    }

    [[nodiscard]] static constexpr bool comparable(element_type a, element_type b) noexcept {
        return leq(a, b) || leq(b, a);
    }

    // Models a local event at process p.  The second precondition is an
    // overflow guard: a slot that wrapped to zero would break monotonicity, so
    // the result would no longer be above its input.
    [[nodiscard]] static constexpr element_type successor_at(element_type v, std::size_t p) noexcept
        pre(::crucible::decide::in_range<std::size_t>(p, 0, N - 1))
            pre(v.clock[p] != std::numeric_limits<std::uint64_t>::max()) {
        v.clock[p] += 1;
        return v;
    }

    // Models a receive event at process me: absorb everything the sender had
    // observed, then count the receive as a local event.
    //
    // Only the index bound is checked here.  The overflow guard belongs to
    // successor_at and is not repeated: the join is computed either way, and
    // its slot for me already equals the larger of the two inputs, so the
    // inner check sees the same value and there is one place to keep correct.
    [[nodiscard]] static constexpr element_type causal_merge(element_type local, element_type received,
                                                             std::size_t me) noexcept
        pre(::crucible::decide::in_range<std::size_t>(me, 0, N - 1)) {
        return successor_at(join(local, received), me);
    }

    [[nodiscard]] static consteval std::string_view name() noexcept { return "HappensBeforeLattice"; }
};

template <typename HB, typename... Slots>
    requires(sizeof...(Slots) == HB::process_count)
         && (std::convertible_to<Slots, typename HB::clock_value_type> && ...)
[[nodiscard]] constexpr typename HB::element_type make_clock(Slots... slots) noexcept {
    return typename HB::element_type{{static_cast<typename HB::clock_value_type>(slots)...}};
}

namespace detail::happens_before_self_test {

using HB4 = HappensBeforeLattice<4>;

static_assert(Lattice<HB4>);
static_assert(BoundedLattice<HB4>);
static_assert(BoundedBelowLattice<HB4>);
static_assert(BoundedAboveLattice<HB4>);

// The negative assertions pin what this lattice is not, so that dropping
// bottom or top silently demotes nothing, and so that no carrier can be
// instantiated over it expecting a multiplicative structure it does not have.
static_assert(!UnboundedLattice<HB4>);
static_assert(!Semiring<HB4>);

static_assert(!std::is_empty_v<HB4::element_type>);
static_assert(sizeof(HB4::element_type) == sizeof(std::uint64_t) * 4);
static_assert(HB4::process_count == 4);

static_assert(std::three_way_comparable<HB4::element_type, std::partial_ordering>);

inline constexpr HB4::element_type hb4_bot{};
inline constexpr HB4::element_type hb4_top = HB4::top();

// A chain: hb4_a precedes hb4_b precedes hb4_c.
inline constexpr HB4::element_type hb4_a{{1, 0, 0, 0}};
inline constexpr HB4::element_type hb4_b{{1, 1, 0, 0}};
inline constexpr HB4::element_type hb4_c{{2, 2, 1, 0}};

// A concurrent pair.
inline constexpr HB4::element_type hb4_x{{2, 0, 1, 0}};
inline constexpr HB4::element_type hb4_y{{0, 2, 0, 1}};

static_assert(verify_bounded_lattice_axioms_at<HB4>(hb4_bot, hb4_bot, hb4_bot));
static_assert(verify_bounded_lattice_axioms_at<HB4>(hb4_bot, hb4_a, hb4_top));
static_assert(verify_bounded_lattice_axioms_at<HB4>(hb4_a, hb4_b, hb4_c));
static_assert(verify_bounded_lattice_axioms_at<HB4>(hb4_x, hb4_y, hb4_top));
static_assert(verify_bounded_lattice_axioms_at<HB4>(hb4_a, hb4_x, hb4_y));

// Each slot is a chain, and a product of distributive lattices is
// distributive, so the pointwise order distributes.
static_assert(verify_distributive_lattice<HB4>(hb4_a, hb4_b, hb4_c));
static_assert(verify_distributive_lattice<HB4>(hb4_x, hb4_y, hb4_a));
static_assert(verify_distributive_lattice<HB4>(hb4_bot, hb4_top, hb4_x));
static_assert(verify_distributive_lattice<HB4>(hb4_a, hb4_a, hb4_y));

static_assert(HB4::leq(hb4_a, hb4_b));
static_assert(HB4::leq(hb4_b, hb4_c));
static_assert(HB4::leq(hb4_a, hb4_c));
static_assert(HB4::happens_before(hb4_a, hb4_b));
static_assert(HB4::happens_before(hb4_b, hb4_c));
static_assert(HB4::happens_before(hb4_a, hb4_c));
static_assert(!HB4::is_concurrent(hb4_a, hb4_b));
static_assert(!HB4::is_concurrent(hb4_a, hb4_c));
static_assert(HB4::comparable(hb4_a, hb4_b));
static_assert(HB4::comparable(hb4_a, hb4_c));

static_assert(!HB4::leq(hb4_b, hb4_a));
static_assert(!HB4::happens_before(hb4_b, hb4_a));

static_assert(HB4::leq(hb4_a, hb4_a));
static_assert(!HB4::happens_before(hb4_a, hb4_a));
static_assert(!HB4::is_concurrent(hb4_a, hb4_a));

// The axiom rollups above already prove the bottom and top identities, but
// they prove them algebraically.  These restate the same facts as order
// questions, which is what catches a leq whose arguments got swapped while the
// identities still hold.
static_assert(HB4::leq(hb4_bot, hb4_a));
static_assert(HB4::leq(hb4_bot, hb4_b));
static_assert(HB4::leq(hb4_bot, hb4_c));
static_assert(HB4::leq(hb4_bot, hb4_x));
static_assert(HB4::leq(hb4_bot, hb4_y));
static_assert(HB4::leq(hb4_bot, hb4_top));
static_assert(HB4::leq(hb4_a, hb4_top));
static_assert(HB4::leq(hb4_b, hb4_top));
static_assert(HB4::leq(hb4_c, hb4_top));
static_assert(HB4::leq(hb4_x, hb4_top));
static_assert(HB4::leq(hb4_y, hb4_top));

// Pinning the two values as well catches a top that returned the zero vector,
// which the algebraic rollups would still accept.
static_assert(hb4_bot == HB4::element_type{{0, 0, 0, 0}});
static_assert(hb4_top
              == HB4::element_type{std::numeric_limits<std::uint64_t>::max(), std::numeric_limits<std::uint64_t>::max(),
                                   std::numeric_limits<std::uint64_t>::max(),
                                   std::numeric_limits<std::uint64_t>::max()});

// All four results of the comparison must be reachable.
static_assert((hb4_bot <=> hb4_bot) == std::partial_ordering::equivalent);
static_assert((hb4_a <=> hb4_a) == std::partial_ordering::equivalent);
static_assert((hb4_a <=> hb4_b) == std::partial_ordering::less);
static_assert((hb4_b <=> hb4_a) == std::partial_ordering::greater);
static_assert((hb4_a <=> hb4_c) == std::partial_ordering::less);
static_assert((hb4_x <=> hb4_y) == std::partial_ordering::unordered);
static_assert((hb4_y <=> hb4_x) == std::partial_ordering::unordered);
static_assert((hb4_bot <=> hb4_top) == std::partial_ordering::less);
static_assert((hb4_top <=> hb4_bot) == std::partial_ordering::greater);

static_assert(hb4_a < hb4_b);
static_assert(hb4_a <= hb4_b);
static_assert(hb4_b > hb4_a);
static_assert(hb4_a == hb4_a);
static_assert(!(hb4_x < hb4_y));
static_assert(!(hb4_x > hb4_y));
static_assert(!(hb4_x == hb4_y));
static_assert(!(hb4_x <= hb4_y));
static_assert(!(hb4_y <= hb4_x));

// The chain and the concurrent pair are not disjoint.  Pinning where they
// meet keeps a rewiring of the witnesses from quietly changing what the
// assertions above test.
static_assert(HB4::leq(hb4_a, hb4_x));
static_assert(HB4::happens_before(hb4_a, hb4_x));
static_assert(!HB4::is_concurrent(hb4_a, hb4_x));

static_assert(HB4::is_concurrent(hb4_a, hb4_y));
static_assert(!HB4::leq(hb4_a, hb4_y));
static_assert(!HB4::leq(hb4_y, hb4_a));

static_assert(HB4::is_concurrent(hb4_b, hb4_y));

// hb4_x is concurrent with hb4_y and still strictly precedes hb4_c, so the
// order has an antichain sitting inside a chain.
static_assert(HB4::leq(hb4_x, hb4_c));
static_assert(HB4::happens_before(hb4_x, hb4_c));
static_assert(!HB4::is_concurrent(hb4_x, hb4_c));

static_assert(!HB4::leq(hb4_x, hb4_y));
static_assert(!HB4::leq(hb4_y, hb4_x));
static_assert(HB4::is_concurrent(hb4_x, hb4_y));
static_assert(HB4::is_concurrent(hb4_y, hb4_x));
static_assert(!HB4::happens_before(hb4_x, hb4_y));
static_assert(!HB4::happens_before(hb4_y, hb4_x));
static_assert(!HB4::comparable(hb4_x, hb4_y));

static_assert(HB4::leq(hb4_x, HB4::join(hb4_x, hb4_y)));
static_assert(HB4::leq(hb4_y, HB4::join(hb4_x, hb4_y)));
static_assert(HB4::join(hb4_x, hb4_y) == HB4::element_type{{2, 2, 1, 1}});

// The meet of two clocks is their latest common ancestor.
static_assert(HB4::meet(hb4_x, hb4_y) == HB4::element_type{{0, 0, 0, 0}});

inline constexpr HB4::element_type hb4_a_after_p0 = HB4::successor_at(hb4_a, 0);
static_assert(hb4_a_after_p0 == HB4::element_type{{2, 0, 0, 0}});
static_assert(HB4::leq(hb4_a, hb4_a_after_p0));
static_assert(HB4::happens_before(hb4_a, hb4_a_after_p0));
static_assert(!HB4::leq(hb4_a_after_p0, hb4_a));

inline constexpr HB4::element_type hb4_a_after_p2 = HB4::successor_at(hb4_a, 2);
static_assert(hb4_a_after_p2 == HB4::element_type{{1, 0, 1, 0}});
static_assert(HB4::leq(hb4_a, hb4_a_after_p2));

// Local events at two processes with no message between them are concurrent.
static_assert(HB4::is_concurrent(hb4_a_after_p0, hb4_a_after_p2));

inline constexpr HB4::element_type hb4_received_y = HB4::causal_merge(hb4_a, hb4_y, 0);
static_assert(hb4_received_y == HB4::element_type{{2, 2, 0, 1}});

static_assert(HB4::leq(hb4_a, hb4_received_y));
static_assert(HB4::leq(hb4_y, hb4_received_y));

static_assert(HB4::happens_before(hb4_a, hb4_received_y));
static_assert(HB4::happens_before(hb4_y, hb4_received_y));

// A single participant is the boundary case that catches any accidental
// assumption of two or more slots.  Its order is total, so nothing is
// concurrent with anything.
using HB1 = HappensBeforeLattice<1>;
inline constexpr HB1::element_type hb1_zero{{0}};
inline constexpr HB1::element_type hb1_one{{1}};
inline constexpr HB1::element_type hb1_two{{2}};

static_assert(HB1::leq(hb1_zero, hb1_one));
static_assert(HB1::leq(hb1_one, hb1_two));
static_assert(HB1::happens_before(hb1_zero, hb1_two));
static_assert(!HB1::is_concurrent(hb1_zero, hb1_one));
static_assert(!HB1::is_concurrent(hb1_one, hb1_two));
static_assert(HB1::comparable(hb1_zero, hb1_two));

static_assert(verify_bounded_lattice_axioms_at<HB1>(hb1_zero, hb1_one, hb1_two));
static_assert(verify_distributive_lattice<HB1>(hb1_zero, hb1_one, hb1_two));

inline constexpr HB1::element_type hb1_zero_after_succ = HB1::successor_at(hb1_zero, 0);
static_assert(hb1_zero_after_succ == HB1::element_type{{1}});
static_assert(hb1_zero_after_succ == hb1_one);
static_assert(HB1::happens_before(hb1_zero, hb1_zero_after_succ));

inline constexpr HB1::element_type hb1_merged = HB1::causal_merge(hb1_one, hb1_two, 0);
static_assert(hb1_merged == HB1::element_type{{3}});
static_assert(HB1::leq(hb1_one, hb1_merged));
static_assert(HB1::leq(hb1_two, hb1_merged));
static_assert(HB1::happens_before(hb1_one, hb1_merged));
static_assert(HB1::happens_before(hb1_two, hb1_merged));

// The unordered result needs one slot where each side leads, which a single
// slot cannot supply, so no comparison here can reach it.
static_assert((hb1_zero <=> hb1_one) == std::partial_ordering::less);
static_assert((hb1_two <=> hb1_one) == std::partial_ordering::greater);
static_assert((hb1_one <=> hb1_one) == std::partial_ordering::equivalent);

struct ReplayClockTag {};
struct KernelOrderClockTag {};

using HBReplay = HappensBeforeLattice<4, ReplayClockTag>;
using HBKernel = HappensBeforeLattice<4, KernelOrderClockTag>;
using HBDefault = HappensBeforeLattice<4>;

static_assert(!std::is_same_v<HBReplay, HBKernel>);
static_assert(!std::is_same_v<HBReplay, HBDefault>);
static_assert(!std::is_same_v<HBKernel, HBDefault>);
static_assert(!std::is_same_v<typename HBReplay::element_type, typename HBKernel::element_type>);

// The tag changes the type without changing the storage.
static_assert(sizeof(HBReplay::element_type) == sizeof(HBKernel::element_type));
static_assert(sizeof(HBReplay::element_type) == sizeof(HBDefault::element_type));

static_assert(HB4::name() == "HappensBeforeLattice");
static_assert(HBReplay::name() == "HappensBeforeLattice");

static_assert(make_clock<HB4>(1, 0, 0, 0) == HB4::element_type{{1, 0, 0, 0}});
static_assert(make_clock<HB4>(0, 0, 0, 0) == HB4::bottom());
static_assert(make_clock<HB4>(2, 2, 1, 0) == hb4_c);
static_assert(make_clock<HB1>(7) == HB1::element_type{{7}});

static_assert(std::is_same_v<decltype(make_clock<HBReplay>(1, 0, 0, 0)), HBReplay::element_type>);
static_assert(!std::is_same_v<decltype(make_clock<HBReplay>(1, 0, 0, 0)), decltype(make_clock<HBKernel>(1, 0, 0, 0))>);

// Calling each operation on runtime operands catches the defects the
// compile-time assertions above cannot see, such as an inline body that only
// ever instantiates in a consteval context.
inline void runtime_smoke_test() {
    HB4::element_type a{{1, 0, 0, 0}};
    HB4::element_type b{{1, 1, 0, 0}};
    HB4::element_type x{{2, 0, 1, 0}};
    HB4::element_type y{{0, 2, 0, 1}};

    [[maybe_unused]] bool l_ab = HB4::leq(a, b);
    [[maybe_unused]] HB4::element_type j_ab = HB4::join(a, b);
    [[maybe_unused]] HB4::element_type m_xy = HB4::meet(x, y);

    [[maybe_unused]] HB4::element_type bot = HB4::bottom();
    [[maybe_unused]] HB4::element_type top = HB4::top();

    [[maybe_unused]] bool hb_ab = HB4::happens_before(a, b);
    [[maybe_unused]] bool conc_xy = HB4::is_concurrent(x, y);
    [[maybe_unused]] bool comp_ab = HB4::comparable(a, b);

    [[maybe_unused]] HB4::element_type succ_a = HB4::successor_at(a, 0);

    [[maybe_unused]] HB4::element_type merged = HB4::causal_merge(a, y, 0);

    // Slot 3 is the high end of the checked bound.
    [[maybe_unused]] std::uint64_t slot0 = a[0];
    [[maybe_unused]] std::uint64_t slot3 = a[3];

    [[maybe_unused]] std::partial_ordering ord_ab = a <=> b;
    [[maybe_unused]] std::partial_ordering ord_xy = x <=> y;
    [[maybe_unused]] bool lt_ab = (a < b);
    [[maybe_unused]] bool ne_xy = !(x == y);

    HB1::element_type s0{{0}};
    HB1::element_type s1{{1}};
    [[maybe_unused]] bool l_s = HB1::leq(s0, s1);
    [[maybe_unused]] HB1::element_type next_s = HB1::successor_at(s0, 0);
    [[maybe_unused]] HB1::element_type merged1 = HB1::causal_merge(s0, s1, 0);
    [[maybe_unused]] std::partial_ordering ord_s1 = s0 <=> s1;
}

}  // namespace detail::happens_before_self_test

}  // namespace crucible::algebra::lattices
