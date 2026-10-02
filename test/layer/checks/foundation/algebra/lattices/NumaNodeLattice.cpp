// The compile-time checks of foundation/algebra/lattices/NumaNodeLattice.h.

#include <foundation/algebra/lattices/NumaNodeLattice.h>

namespace foundation::algebra::lattices {

static_assert(numa_node_id_sentinel_count == std::meta::enumerators_of(^^NumaNodeId).size(),
              "NumaNodeId must hold exactly the sentinels that numa_node_id_sentinel_count counts.  A third one "
              "needs a value that does not collide with the concrete node identifiers, a place in leq, join "
              "and meet, and an update to the initializer of numa_node_id_sentinel_count.");
static_assert(std::to_underlying(NumaNodeId::None) == 254, "NumaNodeId::None must stay 254: every value below it "
                                                           "is a concrete node.");
static_assert(std::to_underlying(NumaNodeId::Any) == 255, "NumaNodeId::Any must stay 255: every value below "
                                                          "None is a concrete node.");

namespace detail::numa_node_lattice_self_test {

static_assert(Lattice<NumaNodeLattice>);
static_assert(BoundedLattice<NumaNodeLattice>);
static_assert(!UnboundedLattice<NumaNodeLattice>);
static_assert(!Semiring<NumaNodeLattice>);

static_assert(sizeof(NumaNodeId) == 1);

static_assert(is_concrete_numa_node(NumaNodeId{0}) && is_concrete_numa_node(NumaNodeId{253}));
static_assert(!is_concrete_numa_node(NumaNodeId::None) && !is_concrete_numa_node(NumaNodeId::Any),
              "a sentinel names no single node, so no memory binds to it");

static_assert(NumaNodeLattice::leq(NumaNodeId::None, NumaNodeId{0}));
static_assert(NumaNodeLattice::leq(NumaNodeId{42}, NumaNodeId::Any));
static_assert(NumaNodeLattice::leq(NumaNodeId{42}, NumaNodeId{42}));
static_assert(!NumaNodeLattice::leq(NumaNodeId{0}, NumaNodeId{1})
                  && !NumaNodeLattice::leq(NumaNodeId{1}, NumaNodeId{0}),
              "two concrete nodes are siblings: neither covers the other");
static_assert(!NumaNodeLattice::leq(NumaNodeId::Any, NumaNodeId{0}));

static_assert(NumaNodeLattice::join(NumaNodeId{0}, NumaNodeId{1}) == NumaNodeId::Any);
static_assert(NumaNodeLattice::join(NumaNodeId{0}, NumaNodeId::None) == NumaNodeId{0});
static_assert(NumaNodeLattice::meet(NumaNodeId{0}, NumaNodeId{1}) == NumaNodeId::None);
static_assert(NumaNodeLattice::meet(NumaNodeId{0}, NumaNodeId::Any) == NumaNodeId{0});

// Three siblings with one top and one bottom are M3.  The distributive
// law fails on them, and this witness pins that shape.  It is why no
// distributivity check appears below, and a product lattice over this
// one inherits the failure.
[[nodiscard]] consteval bool is_not_distributive() noexcept {
    const NumaNodeId first{0};
    const NumaNodeId second{1};
    const NumaNodeId third{2};
    const NumaNodeId lhs = NumaNodeLattice::meet(first, NumaNodeLattice::join(second, third));
    const NumaNodeId rhs =
        NumaNodeLattice::join(NumaNodeLattice::meet(first, second), NumaNodeLattice::meet(first, third));
    return lhs == NumaNodeId{0} && rhs == NumaNodeId::None;
}
static_assert(is_not_distributive(), "NumaNodeLattice must keep its M3 shape.  If this fires, join or meet "
                                     "changed, and the order is no longer siblings between one bottom and one top.");

static_assert(verify_bounded_lattice_axioms_at<NumaNodeLattice>(NumaNodeId::None, NumaNodeId{2}, NumaNodeId::Any));
static_assert(verify_bounded_lattice_axioms_at<NumaNodeLattice>(NumaNodeId{0}, NumaNodeId{1}, NumaNodeId{2}));
static_assert(verify_bounded_lattice_axioms_at<NumaNodeLattice>(NumaNodeId{42}, NumaNodeId{42}, NumaNodeId{43}));
static_assert(verify_bounded_lattice_axioms_at<NumaNodeLattice>(NumaNodeId::None, NumaNodeId::None, NumaNodeId::Any));
static_assert(verify_bounded_lattice_axioms_at<NumaNodeLattice>(NumaNodeId{253}, NumaNodeId::Any, NumaNodeId{0}));

}  // namespace detail::numa_node_lattice_self_test

}  // namespace foundation::algebra::lattices
