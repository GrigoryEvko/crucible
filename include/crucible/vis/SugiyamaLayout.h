#pragma once

// SugiyamaLayout: layered graph drawing for DAGs.
//
// Classic four-phase algorithm:
//   1. Layer assignment   — longest path from sources
//   2. Crossing minimize  — barycenter heuristic (iterative up/down sweeps)
//   3. X-coordinate       — median positioning within layers
//   4. Compaction         — remove horizontal gaps
//
// Handles both intra-block (~50 ops) and inter-block (~160 blocks) layout.
// Performance target: <10ms for 12K ops across 160 blocks.
//
// Not hot path — std::vector is appropriate.

#include <crucible/vis/NetworkSimplex.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <vector>

namespace crucible::vis {

// ═══════════════════════════════════════════════════════════════════
// Input: a DAG as adjacency lists
// ═══════════════════════════════════════════════════════════════════

struct LayoutEdge {
  uint32_t src = 0;
  uint32_t dst = 0;
};

struct LayoutNode {
  // Input (caller fills these):
  float min_width = 60;     // minimum node width (from label length)
  float min_height = 28;    // minimum node height

  // Output (layout fills these):
  float x = 0;              // center x
  float y = 0;              // top y
  uint32_t layer = 0;       // assigned layer (0 = top)
  uint32_t order = 0;       // position within layer (0 = leftmost)
};

// ═══════════════════════════════════════════════════════════════════
// Layout result
// ═══════════════════════════════════════════════════════════════════

struct LayoutResult {
  std::vector<LayoutNode> nodes;
  float total_width = 0;
  float total_height = 0;
  uint32_t num_layers = 0;
};

// ═══════════════════════════════════════════════════════════════════
// Layout parameters
// ═══════════════════════════════════════════════════════════════════

struct LayoutParams {
  float node_h_gap = 16;    // horizontal gap between nodes in same layer
  float layer_v_gap = 36;   // vertical gap between layers
  float padding = 20;       // padding around the entire layout
  bool use_network_simplex = true; // use NS for X positioning (false = naive placement)
};

// ═══════════════════════════════════════════════════════════════════
// Sugiyama layout algorithm
// ═══════════════════════════════════════════════════════════════════

[[nodiscard]] inline LayoutResult sugiyama_layout(
    std::vector<LayoutNode> nodes,
    const std::vector<LayoutEdge>& edges,
    const LayoutParams& params = {}) {

  const uint32_t n = static_cast<uint32_t>(nodes.size());
  if (n == 0) return {.nodes = {}, .total_width = 0, .total_height = 0};

  // ── Phase 1: Layer assignment (longest path from sources) ────────

  // Build adjacency: fwd[u] = successors, rev[u] = predecessors
  std::vector<std::vector<uint32_t>> fwd(n), rev(n);
  std::vector<uint32_t> in_deg(n, 0);
  for (const auto& e : edges) {
    if (e.src < n && e.dst < n && e.src != e.dst) {
      fwd[e.src].push_back(e.dst);
      rev[e.dst].push_back(e.src);
      in_deg[e.dst]++;
    }
  }

  // Topological order via Kahn's algorithm
  std::vector<uint32_t> topo;
  topo.reserve(n);
  {
    std::vector<uint32_t> queue;
    for (uint32_t i = 0; i < n; i++)
      if (in_deg[i] == 0) queue.push_back(i);

    std::vector<uint32_t> deg = in_deg;
    while (!queue.empty()) {
      uint32_t u = queue.back();
      queue.pop_back();
      topo.push_back(u);
      for (uint32_t v : fwd[u]) {
        if (--deg[v] == 0) queue.push_back(v);
      }
    }
  }

  // Longest path layer assignment (in topological order)
  for (uint32_t u : topo) {
    uint32_t max_pred = 0;
    for (uint32_t p : rev[u])
      max_pred = std::max(max_pred, nodes[p].layer + 1);
    nodes[u].layer = max_pred;
  }

  uint32_t num_layers = 0;
  for (const auto& nd : nodes)
    num_layers = std::max(num_layers, nd.layer + 1);

  // ── Phase 1.5: Virtual nodes for multi-rank edges ────────────────
  //
  // When an edge spans >1 layer, insert thin dummy nodes at each
  // intermediate layer. Replace the long edge with a chain of
  // single-layer edges. Virtual nodes participate in crossing
  // minimization and coordinate assignment, guiding long edges
  // through intermediate ranks cleanly.

  const uint32_t original_n = static_cast<uint32_t>(nodes.size());
  // Count virtual nodes needed
  uint32_t num_virtual = 0;
  for (const auto& e : edges) {
    if (e.src >= original_n || e.dst >= original_n) continue;
    int32_t span = static_cast<int32_t>(nodes[e.dst].layer) -
                   static_cast<int32_t>(nodes[e.src].layer);
    if (span > 1) num_virtual += static_cast<uint32_t>(span - 1);
  }

  // Reserve space for virtual nodes
  nodes.reserve(original_n + num_virtual);

  // Rebuild adjacency with virtual node chains
  std::vector<std::vector<uint32_t>> fwd2, rev2;
  fwd2.resize(original_n + num_virtual);
  rev2.resize(original_n + num_virtual);

  uint32_t vnode_id = original_n;
  for (const auto& e : edges) {
    if (e.src >= original_n || e.dst >= original_n) continue;
    if (e.src == e.dst) continue;

    uint32_t src_layer = nodes[e.src].layer;
    uint32_t dst_layer = nodes[e.dst].layer;
    if (dst_layer <= src_layer) continue;  // skip backward edges

    int32_t span = static_cast<int32_t>(dst_layer - src_layer);
    if (span <= 1) {
      // Single-layer edge: keep as-is
      fwd2[e.src].push_back(e.dst);
      rev2[e.dst].push_back(e.src);
    } else {
      // Multi-layer: insert virtual nodes
      uint32_t prev = e.src;
      for (int32_t k = 1; k < span; k++) {
        LayoutNode vn{};
        vn.min_width = 4;     // thin virtual node
        vn.min_height = 4;
        vn.layer = src_layer + static_cast<uint32_t>(k);
        nodes.push_back(vn);
        uint32_t vid = vnode_id++;
        fwd2[prev].push_back(vid);
        rev2[vid].push_back(prev);
        prev = vid;
      }
      fwd2[prev].push_back(e.dst);
      rev2[e.dst].push_back(prev);
    }
  }

  // Also keep single-span edges that weren't multi-rank
  // (already added above, but edges with dst_layer == src_layer+1
  //  that we skipped in the span>1 path)
  for (const auto& e : edges) {
    if (e.src >= original_n || e.dst >= original_n) continue;
    if (e.src == e.dst) continue;
    uint32_t src_layer = nodes[e.src].layer;
    uint32_t dst_layer = nodes[e.dst].layer;
    if (dst_layer == src_layer + 1) {
      fwd2[e.src].push_back(e.dst);
      rev2[e.dst].push_back(e.src);
    }
  }

  // Replace adjacency lists
  fwd = std::move(fwd2);
  rev = std::move(rev2);

  // Update n to include virtual nodes
  const uint32_t total_n = static_cast<uint32_t>(nodes.size());

  // ── Phase 2: Build layers ────────────────────────────────────────

  // Group nodes by layer (including virtual nodes)
  std::vector<std::vector<uint32_t>> layers(num_layers);
  for (uint32_t i = 0; i < total_n; i++)
    layers[nodes[i].layer].push_back(i);

  // Initial order: preserve input order within each layer
  for (auto& layer : layers) {
    for (uint32_t pos = 0; pos < layer.size(); pos++)
      nodes[layer[pos]].order = pos;
  }

  // ── Phase 3: Crossing minimization (barycenter, 8 sweeps) ───────

  auto barycenter = [&](uint32_t node_id, bool use_pred) -> float {
    const auto& adj = use_pred ? rev[node_id] : fwd[node_id];
    if (adj.empty())
      return static_cast<float>(nodes[node_id].order);
    float sum = 0;
    for (uint32_t a : adj)
      sum += static_cast<float>(nodes[a].order);
    return sum / static_cast<float>(adj.size());
  };

  constexpr uint32_t MAX_SWEEPS = 8;
  for (uint32_t sweep = 0; sweep < MAX_SWEEPS; sweep++) {
    // Down sweep: order each layer by barycenter of predecessors
    for (uint32_t l = 1; l < num_layers; l++) {
      auto& layer = layers[l];
      std::ranges::sort(layer, [&](uint32_t a, uint32_t b) {
        return barycenter(a, true) < barycenter(b, true);
      });
      for (uint32_t pos = 0; pos < layer.size(); pos++)
        nodes[layer[pos]].order = pos;
    }

    // Up sweep: order each layer by barycenter of successors
    for (uint32_t l = num_layers - 1; l > 0; l--) {
      auto& layer = layers[l - 1];
      std::ranges::sort(layer, [&](uint32_t a, uint32_t b) {
        return barycenter(a, false) < barycenter(b, false);
      });
      for (uint32_t pos = 0; pos < layer.size(); pos++)
        nodes[layer[pos]].order = pos;
    }
  }

  // ── Phase 4: X-coordinate assignment via network simplex ──────────
  //
  // Build auxiliary constraint graph:
  //   - Adjacent pairs in each rank: minlen = half-widths + gap (hard sep)
  //   - Original edges: weight = edge weight (spring pulling together)
  // Solve with network simplex for optimal X that minimizes weighted
  // edge bending subject to non-overlap constraints.

  // Sort layers by order before building constraints
  for (uint32_t l = 0; l < num_layers; l++) {
    std::ranges::sort(layers[l], [&](uint32_t a, uint32_t b) {
      return nodes[a].order < nodes[b].order;
    });
  }

  float total_width = 0;

  if (params.use_network_simplex) {
  std::vector<NSEdge> aux_edges;
  aux_edges.reserve(total_n + edges.size());

  // Left-right separation constraints within each rank.
  // x(right) - x(left) >= rw(left) + lw(right) + gap
  // Using half-widths (min_width/2) as lw and rw.
  for (uint32_t l = 0; l < num_layers; l++) {
    const auto& layer = layers[l];
    for (uint32_t j = 1; j < layer.size(); j++) {
      uint32_t left = layer[j - 1];
      uint32_t right = layer[j];
      int32_t sep = static_cast<int32_t>(
          nodes[left].min_width / 2 + nodes[right].min_width / 2
          + params.node_h_gap);
      aux_edges.push_back({left, right, sep, 0});  // hard constraint, no spring
    }
  }

  // Spring edges for original DAG edges (pull connected nodes together)
  for (const auto& e : edges) {
    if (e.src < total_n && e.dst < total_n && e.src != e.dst) {
      // Bidirectional spring: if src is left of dst, pull right;
      // if src is right of dst, pull left. Network simplex handles
      // this by allowing negative rank differences.
      // We model as: both directions with minlen=0, weight=1
      aux_edges.push_back({e.src, e.dst, 0, 1});
    }
  }

  // Solve X positions via network simplex
  auto ns_result = network_simplex(total_n, aux_edges, 200);
  if (ns_result.converged) {
    for (uint32_t i = 0; i < total_n; i++) {
      nodes[i].x = params.padding +
          static_cast<float>(ns_result.rank[i]) + nodes[i].min_width / 2;
    }
    float max_x = 0;
    for (uint32_t i = 0; i < total_n; i++)
      max_x = std::max(max_x, nodes[i].x + nodes[i].min_width / 2);
    total_width = max_x + params.padding;
  } else {
    // NS didn't converge — fall through to naive placement
  }
  } // end if (params.use_network_simplex)

  // Naive left-to-right placement (used when NS disabled or didn't converge)
  if (total_width == 0) {
    std::vector<float> layer_widths(num_layers, 0);
    for (uint32_t l = 0; l < num_layers; l++) {
      float w = 0;
      for (uint32_t idx : layers[l])
        w += nodes[idx].min_width;
      if (!layers[l].empty())
        w += params.node_h_gap * static_cast<float>(layers[l].size() - 1);
      layer_widths[l] = w;
    }
    float max_layer_width = *std::ranges::max_element(layer_widths);
    total_width = max_layer_width + 2 * params.padding;
    for (uint32_t l = 0; l < num_layers; l++) {
      float offset = params.padding + (max_layer_width - layer_widths[l]) / 2;
      float x = offset;
      for (uint32_t idx : layers[l]) {
        nodes[idx].x = x + nodes[idx].min_width / 2;
        x += nodes[idx].min_width + params.node_h_gap;
      }
    }
  }

  // ── Phase 5: Y-coordinate assignment ─────────────────────────────

  // Compute per-layer max height
  std::vector<float> layer_heights(num_layers, 0);
  for (uint32_t i = 0; i < total_n; i++)
    layer_heights[nodes[i].layer] = std::max(
        layer_heights[nodes[i].layer], nodes[i].min_height);

  float y = params.padding;
  std::vector<float> layer_y(num_layers);
  for (uint32_t l = 0; l < num_layers; l++) {
    layer_y[l] = y;
    for (uint32_t idx : layers[l])
      nodes[idx].y = y;
    y += layer_heights[l] + params.layer_v_gap;
  }

  float total_height = y - params.layer_v_gap + params.padding;

  // ── Phase 6: Median improvement (4 sweeps) ─────────────────────────
  //
  // Nudge nodes toward the median of their connected nodes' x positions.
  // After each sweep, enforce minimum separation to prevent overlap.

  for (uint32_t sweep = 0; sweep < 4; sweep++) {
    // Down sweep: nudge toward predecessor median
    for (uint32_t l = 1; l < num_layers; l++) {
      for (uint32_t idx : layers[l]) {
        if (rev[idx].empty()) continue;
        std::vector<float> pred_x;
        pred_x.reserve(rev[idx].size());
        for (uint32_t p : rev[idx])
          pred_x.push_back(nodes[p].x);
        std::ranges::sort(pred_x);
        float median = pred_x[pred_x.size() / 2];
        nodes[idx].x = 0.7f * nodes[idx].x + 0.3f * median;
      }
    }

    // Up sweep: nudge toward successor median
    for (uint32_t l = num_layers - 1; l > 0; l--) {
      for (uint32_t idx : layers[l - 1]) {
        if (fwd[idx].empty()) continue;
        std::vector<float> succ_x;
        succ_x.reserve(fwd[idx].size());
        for (uint32_t s : fwd[idx])
          succ_x.push_back(nodes[s].x);
        std::ranges::sort(succ_x);
        float median = succ_x[succ_x.size() / 2];
        nodes[idx].x = 0.7f * nodes[idx].x + 0.3f * median;
      }
    }

    // ── Overlap enforcement: sweep left→right within each layer ────
    // Guarantees: edge-to-edge distance >= node_h_gap.
    // This is a hard constraint — no node can violate it after this pass.
    for (uint32_t l = 0; l < num_layers; l++) {
      auto& layer = layers[l];
      // Sort by current x position
      std::ranges::sort(layer, [&](uint32_t a, uint32_t b) {
        return nodes[a].x < nodes[b].x;
      });
      // Push right if overlapping
      for (uint32_t j = 1; j < layer.size(); j++) {
        uint32_t prev = layer[j - 1];
        uint32_t curr = layer[j];
        float min_x = nodes[prev].x + nodes[prev].min_width / 2
                     + params.node_h_gap
                     + nodes[curr].min_width / 2;
        if (nodes[curr].x < min_x)
          nodes[curr].x = min_x;
      }
      // Update order to match new x positions
      for (uint32_t pos = 0; pos < layer.size(); pos++)
        nodes[layer[pos]].order = pos;
    }
  }

  // Recompute total width after nudging may have expanded the layout
  float actual_max_x = 0;
  for (uint32_t i = 0; i < total_n; i++)
    actual_max_x = std::max(actual_max_x,
        nodes[i].x + nodes[i].min_width / 2);
  total_width = actual_max_x + params.padding;

  // Strip virtual nodes from output (caller only needs original nodes)
  nodes.resize(original_n);

  return LayoutResult{
      .nodes = std::move(nodes),
      .total_width = total_width,
      .total_height = total_height,
      .num_layers = num_layers,
  };
}

} // namespace crucible::vis
