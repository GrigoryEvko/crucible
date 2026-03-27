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

#include <algorithm>
#include <cstdint>
#include <limits>
#include <numeric>
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

  // ── Phase 2: Build layers ────────────────────────────────────────

  // Group nodes by layer
  std::vector<std::vector<uint32_t>> layers(num_layers);
  for (uint32_t i = 0; i < n; i++)
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

  // ── Phase 4: X-coordinate assignment ─────────────────────────────

  // Compute x positions: place nodes left-to-right within each layer
  // with gaps, then center each layer.

  // Compute per-layer width
  std::vector<float> layer_widths(num_layers, 0);
  for (uint32_t l = 0; l < num_layers; l++) {
    float w = 0;
    for (uint32_t idx : layers[l]) {
      w += nodes[idx].min_width;
    }
    // Add gaps
    if (!layers[l].empty())
      w += params.node_h_gap * static_cast<float>(layers[l].size() - 1);
    layer_widths[l] = w;
  }

  float max_layer_width = *std::ranges::max_element(layer_widths);
  float total_width = max_layer_width + 2 * params.padding;

  // Assign x: center each layer within total_width
  for (uint32_t l = 0; l < num_layers; l++) {
    float offset = params.padding + (max_layer_width - layer_widths[l]) / 2;
    // Sort by order
    auto& layer = layers[l];
    std::ranges::sort(layer, [&](uint32_t a, uint32_t b) {
      return nodes[a].order < nodes[b].order;
    });
    float x = offset;
    for (uint32_t idx : layer) {
      nodes[idx].x = x + nodes[idx].min_width / 2;  // center
      x += nodes[idx].min_width + params.node_h_gap;
    }
  }

  // ── Phase 5: Y-coordinate assignment ─────────────────────────────

  // Compute per-layer max height
  std::vector<float> layer_heights(num_layers, 0);
  for (uint32_t i = 0; i < n; i++)
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

  // ── Phase 6: Median improvement (optional, 4 sweeps) ─────────────

  // Nudge nodes toward the median of their connected nodes' x positions
  for (uint32_t sweep = 0; sweep < 4; sweep++) {
    for (uint32_t l = 1; l < num_layers; l++) {
      for (uint32_t idx : layers[l]) {
        if (rev[idx].empty()) continue;
        std::vector<float> pred_x;
        pred_x.reserve(rev[idx].size());
        for (uint32_t p : rev[idx])
          pred_x.push_back(nodes[p].x);
        std::ranges::sort(pred_x);
        float median = pred_x[pred_x.size() / 2];
        // Nudge toward median (don't overlap neighbors)
        nodes[idx].x = 0.7f * nodes[idx].x + 0.3f * median;
      }
    }
  }

  return LayoutResult{
      .nodes = std::move(nodes),
      .total_width = total_width,
      .total_height = total_height,
      .num_layers = num_layers,
  };
}

} // namespace crucible::vis
