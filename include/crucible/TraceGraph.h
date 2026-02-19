#pragma once

#include <cstdint>
#include <cstring>
#include <span>

#include <crucible/Arena.h>
#include <crucible/MerkleDag.h>

namespace crucible {

// ═══════════════════════════════════════════════════════════════════
// Edge types in the property graph
// ═══════════════════════════════════════════════════════════════════

enum class EdgeKind : uint8_t {
  DATA_FLOW,     // Tensor produced by src consumed by dst
  ALIAS,         // Same data_ptr from two different ops (views, in-place)
  CONTROL_FLOW,  // Explicit execution ordering (branch targets)
  SCALAR_FLOW,   // Scalar value dependency (e.g. loss.item() → scalar consumer)
};

// One edge in the property graph. Port-level granularity:
// src_port = which output of src, dst_port = which input of dst.
struct Edge {
  uint32_t src;       // 4B — source op index
  uint32_t dst;       // 4B — destination op index
  uint8_t src_port;   // 1B — output index of src
  uint8_t dst_port;   // 1B — input index of dst
  EdgeKind kind;      // 1B
  uint8_t pad;        // 1B
};

static_assert(sizeof(Edge) == 12, "Edge must be 12 bytes");

// ═══════════════════════════════════════════════════════════════════
// TraceGraph: CSR property graph over a recorded iteration
//
// Combines data-flow edges (tensor producer→consumer) and alias
// edges (shared storage detection) into a bidirectional adjacency
// structure. All memory is arena-allocated.
//
// Forward edges (sorted by src): "who consumes my outputs?"
// Reverse edges (sorted by dst): "who produces my inputs?"
//
// Built once per iteration on the background thread. Used by
// fusion, scheduling, and buffer allocation in later phases.
// ═══════════════════════════════════════════════════════════════════

struct TraceGraph {
  // Nodes (ops in trace order).
  TraceEntry* ops;
  uint32_t num_ops;

  // Forward CSR: edges sorted by src.
  Edge* fwd_edges;
  uint32_t* fwd_offsets; // num_ops + 1 entries

  // Reverse CSR: edges sorted by dst.
  Edge* rev_edges;
  uint32_t* rev_offsets; // num_ops + 1 entries

  uint32_t num_edges;

  // Liveness analysis results (populated by build_trace Phase 3).
  TensorSlot* slots;        // arena-allocated array of all tensor slots
  uint32_t num_slots;       // total unique storages identified

  // ── Forward queries (src → dst): "who consumes op i's outputs?" ──
  [[nodiscard]] const Edge* fwd_begin(uint32_t i) const {
    return fwd_edges + fwd_offsets[i];
  }
  [[nodiscard]] const Edge* fwd_end(uint32_t i) const {
    return fwd_edges + fwd_offsets[i + 1];
  }
  [[nodiscard]] uint32_t out_degree(uint32_t i) const {
    return fwd_offsets[i + 1] - fwd_offsets[i];
  }

  // ── Reverse queries (dst → src): "who produces op i's inputs?" ──
  [[nodiscard]] const Edge* rev_begin(uint32_t i) const {
    return rev_edges + rev_offsets[i];
  }
  [[nodiscard]] const Edge* rev_end(uint32_t i) const {
    return rev_edges + rev_offsets[i + 1];
  }
  [[nodiscard]] uint32_t in_degree(uint32_t i) const {
    return rev_offsets[i + 1] - rev_offsets[i];
  }

  // ── Node access ──
  [[nodiscard]] const TraceEntry& op(uint32_t i) const { return ops[i]; }
};

// ═══════════════════════════════════════════════════════════════════
// Build CSR from a flat edge array via counting sort. O(V + E).
//
// The flat edge array is consumed (not modified). All output arrays
// are arena-allocated. The graph struct itself must be pre-allocated
// by the caller (arena or stack).
// ═══════════════════════════════════════════════════════════════════

inline void build_csr(
    Arena& arena,
    TraceGraph* graph,
    const Edge* edges,
    uint32_t num_edges,
    uint32_t num_ops) {
  graph->num_edges = num_edges;

  // Allocate CSR arrays.
  graph->fwd_edges = arena.alloc_array<Edge>(num_edges);
  graph->fwd_offsets = arena.alloc_array<uint32_t>(num_ops + 1);
  graph->rev_edges = arena.alloc_array<Edge>(num_edges);
  graph->rev_offsets = arena.alloc_array<uint32_t>(num_ops + 1);

  // Count degrees.
  std::memset(graph->fwd_offsets, 0, (num_ops + 1) * sizeof(uint32_t));
  std::memset(graph->rev_offsets, 0, (num_ops + 1) * sizeof(uint32_t));

  for (uint32_t e = 0; e < num_edges; e++) {
    graph->fwd_offsets[edges[e].src + 1]++;
    graph->rev_offsets[edges[e].dst + 1]++;
  }

  // Prefix sum → offsets.
  for (uint32_t i = 1; i <= num_ops; i++) {
    graph->fwd_offsets[i] += graph->fwd_offsets[i - 1];
    graph->rev_offsets[i] += graph->rev_offsets[i - 1];
  }

  // Scatter edges into sorted positions (arena-allocated cursors).
  auto* fwd_cursor = arena.alloc_array<uint32_t>(num_ops);
  auto* rev_cursor = arena.alloc_array<uint32_t>(num_ops);
  std::memcpy(fwd_cursor, graph->fwd_offsets, num_ops * sizeof(uint32_t));
  std::memcpy(rev_cursor, graph->rev_offsets, num_ops * sizeof(uint32_t));

  for (uint32_t e = 0; e < num_edges; e++) {
    graph->fwd_edges[fwd_cursor[edges[e].src]++] = edges[e];
    graph->rev_edges[rev_cursor[edges[e].dst]++] = edges[e];
  }
}

} // namespace crucible
