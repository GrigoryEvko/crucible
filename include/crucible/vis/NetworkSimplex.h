#pragma once

// NetworkSimplex: minimum-cost rank assignment for DAGs.
//
// Solves: minimize Σ weight(e) * |rank(head) - rank(tail)|
//         subject to: rank(head) - rank(tail) >= minlen(e) for all edges
//
// Used for:
//   - Vertical rank assignment (Y coordinates in Sugiyama layout)
//   - Horizontal coordinate assignment (X positioning with constraints)
//
// Algorithm: network simplex on the dual of the rank LP.
//   1. Find feasible initial ranks (longest path from sources)
//   2. Build feasible spanning tree of tight edges (slack = 0)
//   3. Compute cutvalues for all tree edges
//   4. Pivot: swap leaving edge (most negative cutvalue) with entering
//      edge (minimum slack non-tree edge) until optimal
//
// Standalone C++26 implementation — no Graphviz dependencies.
// Not hot path — std::vector is appropriate.

#include <algorithm>
#include <cstdint>
#include <limits>
#include <numeric>
#include <vector>

namespace crucible::vis {

// ═══════════════════════════════════════════════════════════════════
// Input: weighted directed graph with minimum length constraints
// ═══════════════════════════════════════════════════════════════════

struct NSEdge {
  uint32_t tail = 0;       // source node
  uint32_t head = 0;       // destination node
  int32_t minlen = 1;      // minimum rank difference (head - tail >= minlen)
  int32_t weight = 1;      // cost weight (higher = straighter)
};

// ═══════════════════════════════════════════════════════════════════
// Output: rank assignment per node
// ═══════════════════════════════════════════════════════════════════

struct NSResult {
  std::vector<int32_t> rank;   // rank[node_id] = assigned rank
  int32_t min_rank = 0;
  int32_t max_rank = 0;
  bool converged = false;
};

// ═══════════════════════════════════════════════════════════════════
// Network Simplex solver
// ═══════════════════════════════════════════════════════════════════

[[nodiscard]] inline NSResult network_simplex(
    uint32_t num_nodes,
    const std::vector<NSEdge>& edges,
    uint32_t max_iterations = 1000) {

  NSResult result;
  result.rank.resize(num_nodes, 0);

  if (num_nodes == 0 || edges.empty()) {
    result.converged = true;
    return result;
  }

  const uint32_t n = num_nodes;
  const uint32_t m = static_cast<uint32_t>(edges.size());

  // ── Phase 1: Feasible initial ranks (longest path from sources) ────

  // Build adjacency
  std::vector<std::vector<uint32_t>> fwd(n);   // outgoing edge indices
  std::vector<std::vector<uint32_t>> rev(n);   // incoming edge indices
  std::vector<uint32_t> in_deg(n, 0);

  for (uint32_t i = 0; i < m; i++) {
    const auto& e = edges[i];
    if (e.tail < n && e.head < n && e.tail != e.head) {
      fwd[e.tail].push_back(i);
      rev[e.head].push_back(i);
      in_deg[e.head]++;
    }
  }

  // Topological order (Kahn's)
  std::vector<uint32_t> topo;
  {
    std::vector<uint32_t> queue;
    for (uint32_t i = 0; i < n; i++)
      if (in_deg[i] == 0) queue.push_back(i);
    auto deg = in_deg;
    while (!queue.empty()) {
      uint32_t u = queue.back();
      queue.pop_back();
      topo.push_back(u);
      for (uint32_t ei : fwd[u]) {
        uint32_t v = edges[ei].head;
        if (--deg[v] == 0) queue.push_back(v);
      }
    }
  }

  // If graph has cycles, can't solve — return initial ranks
  if (topo.size() != n) {
    result.converged = false;
    return result;
  }

  // Longest path assignment (forward pass)
  auto& rank = result.rank;
  for (uint32_t u : topo) {
    for (uint32_t ei : fwd[u]) {
      const auto& e = edges[ei];
      rank[e.head] = std::max(rank[e.head], rank[e.tail] + e.minlen);
    }
  }

  // ── Phase 2: Feasible spanning tree ────────────────────────────────
  //
  // Find a spanning tree of tight edges (slack = 0).
  // Slack(e) = rank[head] - rank[tail] - minlen

  auto slack = [&](uint32_t ei) -> int32_t {
    const auto& e = edges[ei];
    return rank[e.head] - rank[e.tail] - e.minlen;
  };

  // Tree membership
  std::vector<bool> in_tree(m, false);     // edge in spanning tree?
  std::vector<bool> node_in_tree(n, false);
  std::vector<uint32_t> tree_edges;

  // Parent in tree (for DFS traversal)
  std::vector<int32_t> parent_edge(n, -1); // edge index connecting to parent
  std::vector<int32_t> parent_node(n, -1);

  // BFS to find tight edges for spanning tree
  {
    // Start from all sources
    std::vector<uint32_t> queue;
    for (uint32_t i = 0; i < n; i++) {
      if (in_deg[i] == 0) {
        node_in_tree[i] = true;
        queue.push_back(i);
      }
    }

    while (!queue.empty() && tree_edges.size() < n - 1) {
      uint32_t u = queue.back();
      queue.pop_back();

      for (uint32_t ei : fwd[u]) {
        uint32_t v = edges[ei].head;
        if (!node_in_tree[v] && slack(ei) == 0) {
          in_tree[ei] = true;
          node_in_tree[v] = true;
          tree_edges.push_back(ei);
          parent_edge[v] = static_cast<int32_t>(ei);
          parent_node[v] = static_cast<int32_t>(u);
          queue.push_back(v);
        }
      }
      for (uint32_t ei : rev[u]) {
        uint32_t v = edges[ei].tail;
        if (!node_in_tree[v] && slack(ei) == 0) {
          in_tree[ei] = true;
          node_in_tree[v] = true;
          tree_edges.push_back(ei);
          parent_edge[v] = static_cast<int32_t>(ei);
          parent_node[v] = static_cast<int32_t>(u);
          queue.push_back(v);
        }
      }
    }

    // If spanning tree incomplete, add min-slack edges to connect
    // remaining components
    for (uint32_t attempts = 0;
         tree_edges.size() < n - 1 && attempts < n; attempts++) {
      int32_t best_slack = std::numeric_limits<int32_t>::max();
      uint32_t best_ei = 0;
      for (uint32_t ei = 0; ei < m; ei++) {
        if (in_tree[ei]) continue;
        const auto& e = edges[ei];
        bool tail_in = node_in_tree[e.tail];
        bool head_in = node_in_tree[e.head];
        if (tail_in != head_in) {
          int32_t s = slack(ei);
          if (s < best_slack) {
            best_slack = s;
            best_ei = ei;
          }
        }
      }
      if (best_slack == std::numeric_limits<int32_t>::max()) break;

      // Adjust ranks to make this edge tight
      const auto& be = edges[best_ei];
      if (node_in_tree[be.tail] && !node_in_tree[be.head]) {
        // head not in tree — shift head's component down
        rank[be.head] = rank[be.tail] + be.minlen;
        node_in_tree[be.head] = true;
      } else {
        rank[be.tail] = rank[be.head] - be.minlen;
        node_in_tree[be.tail] = true;
      }
      in_tree[best_ei] = true;
      tree_edges.push_back(best_ei);
      parent_edge[be.head] = static_cast<int32_t>(best_ei);
      parent_node[be.head] = static_cast<int32_t>(be.tail);
    }
  }

  // ── Phase 3: Compute cutvalues ─────────────────────────────────────
  //
  // For each tree edge e, cutvalue(e) = sum of weights of edges crossing
  // the cut defined by removing e from the tree. Positive direction =
  // same as e, negative = opposite.

  std::vector<int32_t> cutvalue(m, 0);

  // DFS postorder for cutvalue computation
  // Simplified: for each tree edge, count non-tree edges that cross it
  auto compute_cutvalues = [&]() {
    for (uint32_t ei : tree_edges) {
      const auto& te = edges[ei];
      // The cut divides tree into two components: head-side and tail-side.
      // Mark head-side via BFS in tree excluding this edge.
      std::vector<bool> head_side(n, false);
      {
        std::vector<uint32_t> q = {te.head};
        head_side[te.head] = true;
        while (!q.empty()) {
          uint32_t u = q.back();
          q.pop_back();
          for (uint32_t tei : tree_edges) {
            if (tei == ei) continue;
            if (!in_tree[tei]) continue;
            const auto& e2 = edges[tei];
            if (e2.tail == u && !head_side[e2.head]) {
              head_side[e2.head] = true;
              q.push_back(e2.head);
            }
            if (e2.head == u && !head_side[e2.tail]) {
              head_side[e2.tail] = true;
              q.push_back(e2.tail);
            }
          }
        }
      }

      // Cutvalue = Σ w(e) for edges tail→head side - Σ w(e) for head→tail side
      int32_t cv = 0;
      for (uint32_t i = 0; i < m; i++) {
        const auto& e2 = edges[i];
        bool t_head = head_side[e2.tail];
        bool h_head = head_side[e2.head];
        if (!t_head && h_head) cv += e2.weight;   // same direction as tree edge
        if (t_head && !h_head) cv -= e2.weight;   // opposite direction
      }
      cutvalue[ei] = cv;
    }
  };

  compute_cutvalues();

  // ── Phase 4: Pivot loop ────────────────────────────────────────────

  for (uint32_t iter = 0; iter < max_iterations; iter++) {
    // Find leaving edge: tree edge with most negative cutvalue
    int32_t worst_cv = 0;
    uint32_t leave_ei = UINT32_MAX;
    for (uint32_t tei : tree_edges) {
      if (cutvalue[tei] < worst_cv) {
        worst_cv = cutvalue[tei];
        leave_ei = tei;
      }
    }

    if (leave_ei == UINT32_MAX) {
      // All cutvalues >= 0 → optimal
      result.converged = true;
      break;
    }

    // Find entering edge: non-tree edge with minimum slack that crosses
    // the same cut as the leaving edge (from tail-side to head-side).
    // Reuse the head_side computation from cutvalue.
    const auto& le = edges[leave_ei];
    std::vector<bool> head_side(n, false);
    {
      std::vector<uint32_t> q = {le.head};
      head_side[le.head] = true;
      while (!q.empty()) {
        uint32_t u = q.back();
        q.pop_back();
        for (uint32_t tei : tree_edges) {
          if (tei == leave_ei || !in_tree[tei]) continue;
          const auto& e2 = edges[tei];
          if (e2.tail == u && !head_side[e2.head]) {
            head_side[e2.head] = true;
            q.push_back(e2.head);
          }
          if (e2.head == u && !head_side[e2.tail]) {
            head_side[e2.tail] = true;
            q.push_back(e2.tail);
          }
        }
      }
    }

    int32_t best_slack = std::numeric_limits<int32_t>::max();
    uint32_t enter_ei = UINT32_MAX;
    for (uint32_t i = 0; i < m; i++) {
      if (in_tree[i]) continue;
      const auto& e2 = edges[i];
      // Must cross the cut in the right direction
      if (!head_side[e2.tail] && head_side[e2.head]) {
        int32_t s = slack(i);
        if (s < best_slack) {
          best_slack = s;
          enter_ei = i;
        }
      }
    }

    if (enter_ei == UINT32_MAX) break;  // shouldn't happen if feasible

    // Pivot: adjust ranks by the slack of the entering edge
    if (best_slack != 0) {
      // Shift head-side component down by best_slack
      for (uint32_t i = 0; i < n; i++) {
        if (head_side[i])
          rank[i] -= best_slack;
      }
    }

    // Swap tree edges
    in_tree[leave_ei] = false;
    in_tree[enter_ei] = true;
    tree_edges.erase(
        std::find(tree_edges.begin(), tree_edges.end(), leave_ei));
    tree_edges.push_back(enter_ei);

    // Recompute cutvalues (expensive but correct)
    compute_cutvalues();
  }

  // Normalize: shift so min rank = 0
  result.min_rank = *std::ranges::min_element(rank);
  result.max_rank = *std::ranges::max_element(rank);
  for (auto& r : rank) r -= result.min_rank;
  result.max_rank -= result.min_rank;
  result.min_rank = 0;

  return result;
}

} // namespace crucible::vis
