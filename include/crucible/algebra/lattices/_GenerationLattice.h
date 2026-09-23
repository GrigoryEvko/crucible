#pragma once

// Bounded chain over a node's generation: the restart counter each node
// advances on its own, every time it comes back up.
//
// The order is numeric, so an older generation sits below a newer one
// and the join of two is the more recent.  The counter is local to one
// node, unlike the fleet-wide epoch it is usually carried beside, and
// the two say different things about the same value.
//
// That is why this is its own type rather than a plain integer.  The
// counter it travels with is also a 64-bit unsigned quantity, so a call
// site that swapped the two would still compile and every later
// comparison would silently read the wrong one.

#include <crucible/algebra/_Graded.h>
#include <crucible/algebra/_Lattice.h>

#include <compare>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string_view>
#include <type_traits>

namespace crucible::algebra::lattices {

struct Generation {
    std::uint64_t value{0};

    [[nodiscard]] constexpr bool operator==(Generation const&) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(Generation const&) const noexcept = default;

    [[nodiscard]] constexpr operator std::uint64_t() const noexcept { return value; }
};

struct GenerationLattice {
    using element_type = Generation;

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

    [[nodiscard]] static consteval std::string_view name() noexcept { return "GenerationLattice"; }
};

namespace detail::generation_lattice_self_test {

static_assert(Lattice<GenerationLattice>);
static_assert(BoundedLattice<GenerationLattice>);
static_assert(!UnboundedLattice<GenerationLattice>);
static_assert(!Semiring<GenerationLattice>);

static_assert(sizeof(Generation) == sizeof(std::uint64_t));
static_assert(std::is_trivially_copyable_v<Generation>);
static_assert(std::is_standard_layout_v<Generation>);

static_assert(!std::is_same_v<Generation, std::uint64_t>);

// There is no assertion here that this counter differs from the sibling
// axis, because the sibling type is not in scope in this header.  That
// assertion belongs where both types are guaranteed present.

static_assert(GenerationLattice::leq(Generation{0}, Generation{1024}));
static_assert(GenerationLattice::leq(Generation{42}, Generation{42}));
static_assert(!GenerationLattice::leq(Generation{2048}, Generation{1024}));

static_assert(GenerationLattice::bottom().value == 0);
static_assert(GenerationLattice::top().value == std::numeric_limits<std::uint64_t>::max());

static_assert(GenerationLattice::join(Generation{1}, Generation{5}).value == 5);
static_assert(GenerationLattice::join(Generation{5}, Generation{1}).value == 5);
static_assert(GenerationLattice::meet(Generation{1}, Generation{5}).value == 1);

static_assert(GenerationLattice::join(Generation{7}, GenerationLattice::bottom()) == Generation{7});
static_assert(GenerationLattice::meet(Generation{7}, GenerationLattice::top()) == Generation{7});

static_assert(GenerationLattice::join(Generation{99}, Generation{99}).value == 99);
static_assert(GenerationLattice::meet(Generation{99}, Generation{99}).value == 99);

// The interior witnesses matter: bottom and top satisfy distributivity
// for reasons that have nothing to do with the order between them.
[[nodiscard]] consteval bool distributive_witness() noexcept {
    Generation a{1};
    Generation b{4};
    Generation c{16};
    auto lhs = GenerationLattice::meet(a, GenerationLattice::join(b, c));
    auto rhs = GenerationLattice::join(GenerationLattice::meet(a, b), GenerationLattice::meet(a, c));
    return lhs == rhs;
}
static_assert(distributive_witness());

static_assert(verify_bounded_lattice_axioms_at<GenerationLattice>(GenerationLattice::bottom(), Generation{7},
                                                                  GenerationLattice::top()));
static_assert(verify_bounded_lattice_axioms_at<GenerationLattice>(Generation{0}, Generation{1}, Generation{2}));
static_assert(verify_bounded_lattice_axioms_at<GenerationLattice>(Generation{1}, Generation{4}, Generation{16}));
static_assert(verify_distributive_lattice<GenerationLattice>(GenerationLattice::bottom(), Generation{7},
                                                             GenerationLattice::top()));
static_assert(verify_distributive_lattice<GenerationLattice>(Generation{1}, Generation{4}, Generation{16}));
static_assert(verify_distributive_lattice<GenerationLattice>(Generation{99}, Generation{99}, Generation{99}));

static_assert([] consteval {
    Generation g{42};
    std::uint64_t n = g;
    return n == 42;
}());

inline void runtime_smoke_test() {
    Generation bot = GenerationLattice::bottom();
    Generation topv = GenerationLattice::top();
    Generation mid{7};
    [[maybe_unused]] bool l = GenerationLattice::leq(bot, topv);
    [[maybe_unused]] Generation j = GenerationLattice::join(mid, topv);
    [[maybe_unused]] Generation m = GenerationLattice::meet(mid, bot);

    Generation g_initial{0};
    Generation g_after_first_restart{1};
    Generation g_after_second_restart{2};
    Generation most_recent =
        GenerationLattice::join(g_initial, GenerationLattice::join(g_after_first_restart, g_after_second_restart));
    if (most_recent.value != 2u) std::abort();

    std::uint64_t total = mid;
    if (total != 7u) std::abort();

    using GenerationGraded = Graded<ModalityKind::Absolute, GenerationLattice, double>;
    GenerationGraded v{3.14, Generation{8}};
    [[maybe_unused]] auto g = v.grade();
    [[maybe_unused]] auto vp = v.peek();
}

}  // namespace detail::generation_lattice_self_test

}  // namespace crucible::algebra::lattices
