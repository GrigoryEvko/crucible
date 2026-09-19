#pragma once

// Partial order over a NUMA node identifier.  A value graded here
// claims where it may be scheduled: None claims nowhere, Any claims
// every node, and each concrete node claims only itself.
//
// The wildcard is the top, so `leq(request, claim)` reads "the claim
// covers the requested slot".  Two different concrete nodes are
// siblings: neither covers the other, their join is the wildcard and
// their meet is None.
//
// This order is deliberately blind to NUMA distance.  A distance-aware
// order, where one node ranks nearer to a second than to a third, is a
// different lattice and belongs in its own type rather than in a
// rewrite of this one.

#include <crucible/algebra/_Graded.h>
#include <crucible/algebra/_Lattice.h>

#include <cstdint>
#include <cstdlib>
#include <meta>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::algebra::lattices {

// A sparse enum.  The two enumerators are sentinels; values 0 through
// 253 are concrete node identifiers carried by the same type with no
// enumerator of their own.
enum class NumaNodeId : std::uint8_t {
    None = 254,  // bottom: unbound
    Any = 255,  // top: wildcard
};

inline constexpr std::size_t numa_node_id_sentinel_count = std::meta::enumerators_of(^^NumaNodeId).size();

static_assert(numa_node_id_sentinel_count == 2, "NumaNodeId must hold exactly the two sentinels None and Any. "
                                                "A third one needs a value that does not collide with the "
                                                "concrete node identifiers, a place in leq, join and meet, and "
                                                "an update to this count.");

static_assert(std::to_underlying(NumaNodeId::None) == 254, "NumaNodeId::None must remain 254: persisted values and "
                                                           "cross-build hashes encode this byte.");
static_assert(std::to_underlying(NumaNodeId::Any) == 255, "NumaNodeId::Any must remain 255: persisted values and "
                                                          "cross-build hashes encode this byte.");

struct NumaNodeLattice {
    using element_type = NumaNodeId;

    [[nodiscard]] static constexpr element_type bottom() noexcept { return NumaNodeId::None; }
    [[nodiscard]] static constexpr element_type top() noexcept { return NumaNodeId::Any; }

    [[nodiscard]] static constexpr bool leq(element_type a, element_type b) noexcept {
        if (a == b) return true;
        if (a == NumaNodeId::None) return true;
        if (b == NumaNodeId::Any) return true;
        return false;
    }

    [[nodiscard]] static constexpr element_type join(element_type a, element_type b) noexcept {
        if (leq(a, b)) return b;
        if (leq(b, a)) return a;
        return NumaNodeId::Any;
    }

    [[nodiscard]] static constexpr element_type meet(element_type a, element_type b) noexcept {
        if (leq(a, b)) return a;
        if (leq(b, a)) return b;
        return NumaNodeId::None;
    }

    [[nodiscard]] static consteval std::string_view name() noexcept { return "NumaNodeLattice"; }
};

namespace detail::numa_node_lattice_self_test {

static_assert(Lattice<NumaNodeLattice>);
static_assert(BoundedLattice<NumaNodeLattice>);
static_assert(!UnboundedLattice<NumaNodeLattice>);
static_assert(!Semiring<NumaNodeLattice>);

static_assert(sizeof(NumaNodeId) == 1);
static_assert(std::is_trivially_copyable_v<NumaNodeId>);

static_assert(NumaNodeLattice::bottom() == NumaNodeId::None);
static_assert(NumaNodeLattice::top() == NumaNodeId::Any);

static_assert(NumaNodeLattice::leq(NumaNodeId::None, NumaNodeId::None));
static_assert(NumaNodeLattice::leq(NumaNodeId::Any, NumaNodeId::Any));
static_assert(NumaNodeLattice::leq(NumaNodeId{0}, NumaNodeId{0}));
static_assert(NumaNodeLattice::leq(NumaNodeId{42}, NumaNodeId{42}));

static_assert(NumaNodeLattice::leq(NumaNodeId::None, NumaNodeId::Any));
static_assert(NumaNodeLattice::leq(NumaNodeId::None, NumaNodeId{0}));
static_assert(NumaNodeLattice::leq(NumaNodeId::None, NumaNodeId{42}));

static_assert(NumaNodeLattice::leq(NumaNodeId{0}, NumaNodeId::Any));
static_assert(NumaNodeLattice::leq(NumaNodeId{42}, NumaNodeId::Any));
static_assert(NumaNodeLattice::leq(NumaNodeId::None, NumaNodeId::Any));

static_assert(!NumaNodeLattice::leq(NumaNodeId{0}, NumaNodeId{1}));
static_assert(!NumaNodeLattice::leq(NumaNodeId{1}, NumaNodeId{0}));
static_assert(!NumaNodeLattice::leq(NumaNodeId{42}, NumaNodeId{43}));

static_assert(NumaNodeLattice::join(NumaNodeId{0}, NumaNodeId{0}) == NumaNodeId{0});
static_assert(NumaNodeLattice::join(NumaNodeId{0}, NumaNodeId::None) == NumaNodeId{0});
static_assert(NumaNodeLattice::join(NumaNodeId{0}, NumaNodeId::Any) == NumaNodeId::Any);
static_assert(NumaNodeLattice::join(NumaNodeId{0}, NumaNodeId{1}) == NumaNodeId::Any);
static_assert(NumaNodeLattice::join(NumaNodeId::None, NumaNodeId::Any) == NumaNodeId::Any);

static_assert(NumaNodeLattice::meet(NumaNodeId{0}, NumaNodeId{0}) == NumaNodeId{0});
static_assert(NumaNodeLattice::meet(NumaNodeId{0}, NumaNodeId::None) == NumaNodeId::None);
static_assert(NumaNodeLattice::meet(NumaNodeId{0}, NumaNodeId::Any) == NumaNodeId{0});
static_assert(NumaNodeLattice::meet(NumaNodeId{0}, NumaNodeId{1}) == NumaNodeId::None);
static_assert(NumaNodeLattice::meet(NumaNodeId::None, NumaNodeId::Any) == NumaNodeId::None);

static_assert(NumaNodeLattice::join(NumaNodeId{42}, NumaNodeId{42}) == NumaNodeId{42});
static_assert(NumaNodeLattice::meet(NumaNodeId{42}, NumaNodeId{42}) == NumaNodeId{42});

static_assert(NumaNodeLattice::join(NumaNodeId{42}, NumaNodeLattice::bottom()) == NumaNodeId{42});
static_assert(NumaNodeLattice::meet(NumaNodeId{42}, NumaNodeLattice::top()) == NumaNodeId{42});

static_assert(!NumaNodeLattice::leq(NumaNodeId{0}, NumaNodeId{1})
              && !NumaNodeLattice::leq(NumaNodeId{1}, NumaNodeId{0}));

[[nodiscard]] consteval bool transitivity_witness() noexcept {
    NumaNodeId bot = NumaNodeId::None;
    NumaNodeId nodeA{2};
    NumaNodeId topv = NumaNodeId::Any;
    return NumaNodeLattice::leq(bot, nodeA) && NumaNodeLattice::leq(nodeA, topv) && NumaNodeLattice::leq(bot, topv)
        && NumaNodeLattice::leq(bot, bot) && NumaNodeLattice::leq(topv, topv);
}
static_assert(transitivity_witness());

// Three siblings sharing one top and one bottom are the classical M3
// lattice, which is not distributive.  This witness pins that shape, and
// it is why no distributivity check appears anywhere below.  Any product
// lattice built over this one inherits the non-distributivity.
[[nodiscard]] consteval bool non_distributive_witness() noexcept {
    NumaNodeId a{0}, b{1}, c{2};
    auto lhs = NumaNodeLattice::meet(a, NumaNodeLattice::join(b, c));
    auto rhs = NumaNodeLattice::join(NumaNodeLattice::meet(a, b), NumaNodeLattice::meet(a, c));
    return lhs == NumaNodeId{0} && rhs == NumaNodeId::None && lhs != rhs;
}
static_assert(non_distributive_witness(), "NumaNodeLattice must keep its M3 shape. If this fires, join or "
                                          "meet changed and the partial order is no longer three siblings "
                                          "between one bottom and one top.");

[[nodiscard]] consteval bool associativity_witness() noexcept {
    auto check = [](NumaNodeId a, NumaNodeId b, NumaNodeId c) {
        auto lhs_join = NumaNodeLattice::join(NumaNodeLattice::join(a, b), c);
        auto rhs_join = NumaNodeLattice::join(a, NumaNodeLattice::join(b, c));
        auto lhs_meet = NumaNodeLattice::meet(NumaNodeLattice::meet(a, b), c);
        auto rhs_meet = NumaNodeLattice::meet(a, NumaNodeLattice::meet(b, c));
        return lhs_join == rhs_join && lhs_meet == rhs_meet;
    };
    return check(NumaNodeId{0}, NumaNodeId{1}, NumaNodeId{2}) && check(NumaNodeId::None, NumaNodeId{0}, NumaNodeId::Any)
        && check(NumaNodeId{42}, NumaNodeId::Any, NumaNodeId::None)
        && check(NumaNodeId{5}, NumaNodeId{5}, NumaNodeId{6});
}
static_assert(associativity_witness());

[[nodiscard]] consteval bool absorption_witness() noexcept {
    auto check = [](NumaNodeId a, NumaNodeId b) {
        auto lhs1 = NumaNodeLattice::join(a, NumaNodeLattice::meet(a, b));
        auto lhs2 = NumaNodeLattice::meet(a, NumaNodeLattice::join(a, b));
        return lhs1 == a && lhs2 == a;
    };
    return check(NumaNodeId{0}, NumaNodeId{1}) && check(NumaNodeId::None, NumaNodeId::Any)
        && check(NumaNodeId{42}, NumaNodeId::Any) && check(NumaNodeId::None, NumaNodeId{0});
}
static_assert(absorption_witness());

static_assert(verify_bounded_lattice_axioms_at<NumaNodeLattice>(NumaNodeId::None, NumaNodeId{2}, NumaNodeId::Any));
static_assert(verify_bounded_lattice_axioms_at<NumaNodeLattice>(NumaNodeId{0}, NumaNodeId{1}, NumaNodeId{2}));
static_assert(verify_bounded_lattice_axioms_at<NumaNodeLattice>(NumaNodeId{42}, NumaNodeId{42}, NumaNodeId{43}));
static_assert(verify_bounded_lattice_axioms_at<NumaNodeLattice>(NumaNodeId::None, NumaNodeId::None, NumaNodeId::Any));

inline void runtime_smoke_test() {
    NumaNodeId bot = NumaNodeLattice::bottom();
    NumaNodeId topv = NumaNodeLattice::top();
    NumaNodeId node2{2};
    [[maybe_unused]] bool l = NumaNodeLattice::leq(bot, topv);
    [[maybe_unused]] NumaNodeId j = NumaNodeLattice::join(node2, topv);
    [[maybe_unused]] NumaNodeId m = NumaNodeLattice::meet(node2, bot);

    NumaNodeId joined_siblings = NumaNodeLattice::join(NumaNodeId{0}, NumaNodeId{1});
    if (joined_siblings != NumaNodeId::Any) std::abort();

    NumaNodeId meet_siblings = NumaNodeLattice::meet(NumaNodeId{0}, NumaNodeId{1});
    if (meet_siblings != NumaNodeId::None) std::abort();

    using NumaNodeGraded = Graded<ModalityKind::Absolute, NumaNodeLattice, int>;
    NumaNodeGraded v{42, NumaNodeId{2}};
    [[maybe_unused]] auto g = v.grade();
    [[maybe_unused]] auto vp = v.peek();
}

}  // namespace detail::numa_node_lattice_self_test

}  // namespace crucible::algebra::lattices
