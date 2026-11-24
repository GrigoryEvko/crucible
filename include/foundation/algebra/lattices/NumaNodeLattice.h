#pragma once

// Partial order over a NUMA node identifier: the FlatLattice of
// NumaNodeId.  None names no node, Any names every node, and each concrete
// node names only itself.
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
// The predicate is_concrete_numa_node refuses the two sentinel values.
// fixy/os/NumaPlacement.h reads it, because the kernel binds memory to a
// concrete node only.  test/foundation/test_lattices_bands.cpp runs the
// lattice at run time.

#include <foundation/ByteSeal.h>
#include <foundation/algebra/Lattice.h>
#include <foundation/algebra/lattices/FlatLattice.h>
#include <foundation/core/Text.h>

#include <cstddef>
#include <cstdint>
#include <meta>
#include <type_traits>
#include <utility>

namespace foundation::algebra::lattices {

// A sparse enum.  The two enumerators are sentinels.  The values 0 thru
// 253 are concrete node identifiers of the same type, with no enumerator
// of their own (open_values of foundation/ByteSeal.h).
enum class[[= ::foundation::core::open_values{}]] NumaNodeId : std::uint8_t {
    None = 254,  // bottom: no node
    Any = 255,  // top: every node
};

inline constexpr std::size_t numa_node_id_sentinel_count = 2;

// True when the identifier names one node, not a sentinel.
[[nodiscard]] constexpr bool is_concrete_numa_node(NumaNodeId node) noexcept {
    return std::to_underlying(node) < std::to_underlying(NumaNodeId::None);
}

// A claim that covers more requested nodes is the stronger claim.
struct NumaNodeLattice : FlatLattice<NumaNodeId, NumaNodeId::None, NumaNodeId::Any> {
    [[nodiscard]] static consteval ::foundation::core::TextView name() noexcept { return "NumaNodeLattice"; }
};

}  // namespace foundation::algebra::lattices
