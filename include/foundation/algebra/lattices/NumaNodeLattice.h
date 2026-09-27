#pragma once

// Partial order over a NUMA node identifier.  None names no node, Any
// names every node, and each concrete node names only itself.
//
// The wildcard is the top, so leq(request, claim) reads "the claim covers
// the requested node".  Two different concrete nodes are siblings.
// Neither covers the other, their join is the wildcard and their meet is
// None.  Three siblings between one bottom and one top are the lattice
// M3, which is not distributive, and this header claims no more than a
// bounded lattice.
//
// The order does not see NUMA distance.  An order in which one node sits
// nearer to a second node than to a third is a different lattice, with
// a type of its own.
//
// Old spelling: include/crucible/algebra/lattices/NumaNodeLattice.h.  The
// order, the two sentinel values and the witnesses are the same.  The
// runtime smoke test moved to test/foundation/test_lattices_bands.cpp,
// and the predicate is_concrete_numa_node is new.  fixy/os/NumaPlacement.h
// reads it, because the kernel binds memory to a concrete node only.

#include <foundation/algebra/Lattice.h>

#include <cstddef>
#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>
#include <utility>

namespace foundation::algebra::lattices {

// A sparse enum.  The two enumerators are sentinels.  The values 0 thru
// 253 are concrete node identifiers of the same type, with no enumerator
// of their own.
enum class NumaNodeId : std::uint8_t {
    None = 254,  // bottom: no node
    Any = 255,  // top: every node
};

inline constexpr std::size_t numa_node_id_sentinel_count = std::meta::enumerators_of(^^NumaNodeId).size();

static_assert(numa_node_id_sentinel_count == 2, "NumaNodeId must hold exactly the two sentinels None and Any. "
                                                "A third one needs a value that does not collide with the "
                                                "concrete node identifiers, a place in leq, join and meet, and "
                                                "an update to this count.");
static_assert(std::to_underlying(NumaNodeId::None) == 254, "NumaNodeId::None must stay 254: every value below it "
                                                           "is a concrete node.");
static_assert(std::to_underlying(NumaNodeId::Any) == 255, "NumaNodeId::Any must stay 255: every value below "
                                                          "None is a concrete node.");

// True when the identifier names one node, not a sentinel.
[[nodiscard]] constexpr bool is_concrete_numa_node(NumaNodeId node) noexcept {
    return std::to_underlying(node) < std::to_underlying(NumaNodeId::None);
}

struct NumaNodeLattice {
    using element_type = NumaNodeId;

    // A claim that covers more requested nodes is the stronger claim.
    static constexpr ClaimOrientation claim_orientation = ClaimOrientation::stronger_is_higher;

    [[nodiscard]] static constexpr element_type bottom() noexcept { return NumaNodeId::None; }
    [[nodiscard]] static constexpr element_type top() noexcept { return NumaNodeId::Any; }

    [[nodiscard]] static constexpr bool leq(element_type lhs, element_type rhs) noexcept {
        return lhs == rhs || lhs == NumaNodeId::None || rhs == NumaNodeId::Any;
    }

    [[nodiscard]] static constexpr element_type join(element_type lhs, element_type rhs) noexcept {
        if (leq(lhs, rhs)) return rhs;
        if (leq(rhs, lhs)) return lhs;
        return NumaNodeId::Any;
    }

    [[nodiscard]] static constexpr element_type meet(element_type lhs, element_type rhs) noexcept {
        if (leq(lhs, rhs)) return lhs;
        if (leq(rhs, lhs)) return rhs;
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

static_assert(is_concrete_numa_node(NumaNodeId{0}) && is_concrete_numa_node(NumaNodeId{253}));
static_assert(!is_concrete_numa_node(NumaNodeId::None) && !is_concrete_numa_node(NumaNodeId::Any),
              "a sentinel names no single node, so no memory binds to it");

static_assert(NumaNodeLattice::leq(NumaNodeId::None, NumaNodeId{0}));
static_assert(NumaNodeLattice::leq(NumaNodeId{42}, NumaNodeId::Any));
static_assert(NumaNodeLattice::leq(NumaNodeId{42}, NumaNodeId{42}));
static_assert(!NumaNodeLattice::leq(NumaNodeId{0}, NumaNodeId{1}) && !NumaNodeLattice::leq(NumaNodeId{1}, NumaNodeId{0}),
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
