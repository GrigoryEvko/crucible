#pragma once

// Bounded chain over a count of bits transferred on a value's
// production path.
//
// This order runs by consumption, not by claim strength, so the larger
// count is the higher element.  That is the opposite of the tier chains,
// where the strongest claim sits at the top.  The grade here is a
// measured quantity rather than a cap, and the question the order
// answers is whether one footprint is within another.  A gate reading
// "produced at most N bits" then admits exactly when the value's grade
// is below the gate's.
//
// Code that wants the cap reading, where a smaller number is the
// stronger claim, needs its own lattice with the reverse order.  Folding
// both readings into this one would break every existing consumer,
// silently, since the numbers stay identical.

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/Lattice.h>

#include <compare>
#include <cstdint>
#include <cstdlib>  // std::abort in the runtime smoke test
#include <limits>
#include <string_view>
#include <type_traits>

namespace crucible::algebra::lattices {

// A distinct type, not a bare integer, so that a count of bits cannot be
// assigned from a count of anything else that also happens to be a
// 64-bit unsigned quantity.
struct BitsBudget {
    std::uint64_t value{0};

    [[nodiscard]] constexpr bool operator==(BitsBudget const&) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(BitsBudget const&) const noexcept = default;

    // Unwrapping is implicit so that accumulator arithmetic reads
    // normally.  Wrapping stays explicit, which is what keeps an
    // unrelated integer from becoming a budget by accident.
    [[nodiscard]] constexpr operator std::uint64_t() const noexcept { return value; }
};

struct BitsBudgetLattice {
    using element_type = BitsBudget;

    [[nodiscard]] static constexpr element_type bottom() noexcept { return element_type{0}; }
    [[nodiscard]] static constexpr element_type top() noexcept {
        return element_type{std::numeric_limits<std::uint64_t>::max()};
    }
    [[nodiscard]] static constexpr bool leq(element_type a, element_type b) noexcept { return a.value <= b.value; }
    [[nodiscard]] static constexpr element_type join(element_type a, element_type b) noexcept {
        return element_type{a.value >= b.value ? a.value : b.value};
    }
    [[nodiscard]] static constexpr element_type meet(element_type a, element_type b) noexcept {
        return element_type{a.value <= b.value ? a.value : b.value};
    }

    [[nodiscard]] static consteval std::string_view name() noexcept { return "BitsBudgetLattice"; }
};

namespace detail::bits_budget_lattice_self_test {

static_assert(Lattice<BitsBudgetLattice>);
static_assert(BoundedLattice<BitsBudgetLattice>);
static_assert(!UnboundedLattice<BitsBudgetLattice>);
static_assert(!Semiring<BitsBudgetLattice>);

static_assert(sizeof(BitsBudget) == sizeof(std::uint64_t));
static_assert(std::is_trivially_copyable_v<BitsBudget>);
static_assert(std::is_standard_layout_v<BitsBudget>);

static_assert(BitsBudgetLattice::leq(BitsBudget{0}, BitsBudget{1}));
static_assert(BitsBudgetLattice::leq(BitsBudget{1}, BitsBudget{1}));
static_assert(!BitsBudgetLattice::leq(BitsBudget{2}, BitsBudget{1}));
static_assert(BitsBudgetLattice::leq(BitsBudgetLattice::bottom(), BitsBudgetLattice::top()));

static_assert(BitsBudgetLattice::bottom().value == 0);
static_assert(BitsBudgetLattice::top().value == std::numeric_limits<std::uint64_t>::max());

static_assert(BitsBudgetLattice::join(BitsBudget{3}, BitsBudget{7}).value == 7);
static_assert(BitsBudgetLattice::join(BitsBudget{7}, BitsBudget{3}).value == 7);
static_assert(BitsBudgetLattice::meet(BitsBudget{3}, BitsBudget{7}).value == 3);
static_assert(BitsBudgetLattice::meet(BitsBudget{7}, BitsBudget{3}).value == 3);

static_assert(BitsBudgetLattice::join(BitsBudget{42}, BitsBudgetLattice::bottom()) == BitsBudget{42});
static_assert(BitsBudgetLattice::meet(BitsBudget{42}, BitsBudgetLattice::top()) == BitsBudget{42});
static_assert(BitsBudgetLattice::join(BitsBudgetLattice::top(), BitsBudget{42}) == BitsBudgetLattice::top());
static_assert(BitsBudgetLattice::meet(BitsBudgetLattice::bottom(), BitsBudget{42}) == BitsBudgetLattice::bottom());

static_assert(BitsBudgetLattice::join(BitsBudget{99}, BitsBudget{99}).value == 99);
static_assert(BitsBudgetLattice::meet(BitsBudget{99}, BitsBudget{99}).value == 99);

static_assert(BitsBudgetLattice::leq(BitsBudget{5}, BitsBudget{5}));
static_assert(!(BitsBudget{5} != BitsBudget{5}));

// The interior witnesses matter: bottom and top satisfy distributivity
// for reasons that have nothing to do with the order between them.
[[nodiscard]] consteval bool distributive_witness() noexcept {
    BitsBudget a{2};
    BitsBudget b{5};
    BitsBudget c{8};
    auto lhs = BitsBudgetLattice::meet(a, BitsBudgetLattice::join(b, c));
    auto rhs = BitsBudgetLattice::join(BitsBudgetLattice::meet(a, b), BitsBudgetLattice::meet(a, c));
    return lhs == rhs;
}
static_assert(distributive_witness());

static_assert(verify_bounded_lattice_axioms_at<BitsBudgetLattice>(BitsBudgetLattice::bottom(), BitsBudget{1024},
                                                                  BitsBudgetLattice::top()));
static_assert(verify_bounded_lattice_axioms_at<BitsBudgetLattice>(BitsBudget{0}, BitsBudget{42}, BitsBudget{99}));
static_assert(verify_bounded_lattice_axioms_at<BitsBudgetLattice>(BitsBudget{1}, BitsBudget{2}, BitsBudget{3}));
static_assert(verify_distributive_lattice<BitsBudgetLattice>(BitsBudgetLattice::bottom(), BitsBudget{1024},
                                                             BitsBudgetLattice::top()));
static_assert(verify_distributive_lattice<BitsBudgetLattice>(BitsBudget{2}, BitsBudget{5}, BitsBudget{8}));
static_assert(verify_distributive_lattice<BitsBudgetLattice>(BitsBudget{99}, BitsBudget{99}, BitsBudget{99}));

static_assert(!std::is_same_v<BitsBudget, std::uint64_t>);

static_assert([] consteval {
    BitsBudget b{42};
    std::uint64_t n = b;
    return n == 42;
}());

// The opposite direction has no witness here.  Asserting that a bare
// integer does not convert to a budget would require writing the
// rejected conversion, which does not compile.  A negative-compile
// fixture covers it instead.

inline void runtime_smoke_test() {
    BitsBudget bot = BitsBudgetLattice::bottom();
    BitsBudget topv = BitsBudgetLattice::top();
    BitsBudget mid{1024};
    [[maybe_unused]] bool l = BitsBudgetLattice::leq(bot, topv);
    [[maybe_unused]] BitsBudget j = BitsBudgetLattice::join(mid, topv);
    [[maybe_unused]] BitsBudget m = BitsBudgetLattice::meet(mid, bot);

    BitsBudget step1{4};
    BitsBudget step2{8};
    BitsBudget composed = BitsBudgetLattice::join(step1, step2);
    if (composed.value != 8u) std::abort();

    std::uint64_t total = mid;
    if (total != 1024u) std::abort();

    using BitsBudgetGraded = Graded<ModalityKind::Absolute, BitsBudgetLattice, int>;
    BitsBudgetGraded v{42, BitsBudget{16}};
    [[maybe_unused]] auto g = v.grade();
    [[maybe_unused]] auto vp = v.peek();
}

}  // namespace detail::bits_budget_lattice_self_test

}  // namespace crucible::algebra::lattices
