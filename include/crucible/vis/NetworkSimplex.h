#pragma once

// Minimum-cost rank assignment for a directed acyclic graph.
//
// Minimize the sum over edges of weight(e) * (rank(head) - rank(tail)),
// subject to rank(head) - rank(tail) >= minlen(e) for every edge. The solver
// is the network simplex method of Gansner, Koutsofios, North and Vo, "A
// Technique for Drawing Directed Graphs", IEEE TSE 19(3), 1993, section 2.3.
//
// A spanning tree of tight edges (slack zero) fixes the ranks, one tree for
// each connected component. Without one of its edges, a tree splits into a
// tail component and a head component. The cut value of the edge is the summed
// weight of the edges from the tail component to the head component, minus
// the summed weight of the edges the other way. It is the change of cost when
// the edge becomes one unit longer. A tree edge with a negative cut value
// leaves the tree. The non-tree edge of least slack from the head component to
// the tail component enters, and the head component moves by that slack. The
// tree is optimal when no cut value is negative.
//
// The pivot rule is Bland's rule: the leaving edge is the tree edge of least
// index with a negative cut value, and the entering edge has the least index
// among the edges of least slack. The rule cannot cycle, so the pivot loop
// ends without a limit.
//
// Cost: the first feasible tree is O(V * E) in the worst case, and each pivot
// is O(V + E).
//
// This is not a hot path, so std::vector is appropriate.  The solver is in
// src/vis/NetworkSimplex.cpp.

#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace crucible::vis {

struct NSEdge {
    uint32_t tail = 0;
    uint32_t head = 0;
    int32_t minlen = 1;
    int32_t weight = 1;  // Not negative. A higher weight pulls the edge shorter.
};

enum class NSStatus : uint8_t {
    // The graph has a directed cycle, a self-loop included. Each rank is zero.
    NoRanking,
    // No tree edge has a negative cut value, so the ranks have the least cost.
    Optimal,
    // The pivot count got to max_pivots. The ranks satisfy each minlen, but can cost more.
    PivotLimit,
};

struct NSResult {
    std::vector<int64_t> rank;  // One rank for each node. The least rank is zero.
    int64_t max_rank = 0;
    NSStatus status = NSStatus::NoRanking;
};

// The input that network_simplex accepts: each endpoint names a node, no
// weight is negative, and two entries for each edge fit in uint32_t. A
// negative weight can make the cost unbounded.
[[nodiscard]] bool is_well_formed_ns_input(uint32_t num_nodes, std::span<const NSEdge> edges) noexcept;

// Ranks the nodes of a directed acyclic graph at least cost. The result is
// Optimal, or NoRanking for a graph with a cycle, or PivotLimit when the
// optimum needs more than max_pivots pivots.
[[nodiscard]] NSResult network_simplex(uint32_t num_nodes, std::span<const NSEdge> edges,
                                       uint32_t max_pivots = std::numeric_limits<uint32_t>::max())
    pre(is_well_formed_ns_input(num_nodes, edges));

}  // namespace crucible::vis
