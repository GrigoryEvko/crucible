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
  OpIndex src;            // 4B — source op index (default = none)
  OpIndex dst;            // 4B — destination op index (default = none)
  uint8_t src_port = 0;  // 1B — output index of src
  uint8_t dst_port = 0;  // 1B — input index of dst
  EdgeKind kind = EdgeKind::DATA_FLOW; // 1B
  uint8_t pad = 0;       // 1B
};

static_assert(sizeof(Edge) == 12, "Edge must be 12 bytes");
CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE(Edge);

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
  TraceEntry* ops = nullptr;
  uint32_t num_ops = 0;

  // Forward CSR: edges sorted by src.
  Edge* fwd_edges = nullptr;
  uint32_t* fwd_offsets = nullptr; // num_ops + 1 entries

  // Reverse CSR: edges sorted by dst.
  Edge* rev_edges = nullptr;
  uint32_t* rev_offsets = nullptr; // num_ops + 1 entries

  uint32_t num_edges = 0;

  // Liveness analysis results (populated by build_trace Phase 3).
  TensorSlot* slots = nullptr;    // arena-allocated array of all tensor slots
  uint32_t num_slots = 0;         // total unique storages identified

  // Fused content hash — computed during build_trace as a streaming
  // accumulator, avoiding a redundant second pass over all ops.
  ContentHash content_hash;       // 8B

  // Maximum MetaLog index consumed by this trace. Caller advances
  // MetaLog tail AFTER all meta reads are complete (zero-copy safety).
  uint32_t max_meta_end = 0;      // 4B
  uint32_t pad_tg = 0;            // 4B — alignment

  // ── Forward queries (src → dst): "who consumes op i's outputs?" ──
  // gnu::pure: accessors read *this fields + the CSR arrays; no side
  // effects, no memory writes.  Optimizer may CSE across successive
  // calls with the same argument within a basic block.
  [[nodiscard, gnu::pure]] const Edge* fwd_begin(uint32_t i) const noexcept CRUCIBLE_LIFETIMEBOUND {
    return fwd_edges + fwd_offsets[i];
  }
  [[nodiscard, gnu::pure]] const Edge* fwd_end(uint32_t i) const noexcept CRUCIBLE_LIFETIMEBOUND {
    return fwd_edges + fwd_offsets[i + 1];
  }
  [[nodiscard, gnu::pure]] uint32_t out_degree(uint32_t i) const noexcept {
    return fwd_offsets[i + 1] - fwd_offsets[i];
  }

  // ── Reverse queries (dst → src): "who produces op i's inputs?" ──
  [[nodiscard, gnu::pure]] const Edge* rev_begin(uint32_t i) const noexcept CRUCIBLE_LIFETIMEBOUND {
    return rev_edges + rev_offsets[i];
  }
  [[nodiscard, gnu::pure]] const Edge* rev_end(uint32_t i) const noexcept CRUCIBLE_LIFETIMEBOUND {
    return rev_edges + rev_offsets[i + 1];
  }
  [[nodiscard, gnu::pure]] uint32_t in_degree(uint32_t i) const noexcept {
    return rev_offsets[i + 1] - rev_offsets[i];
  }

  // ── Node access ──
  [[nodiscard, gnu::pure]] const TraceEntry& op(uint32_t i) const noexcept CRUCIBLE_LIFETIMEBOUND { return ops[i]; }
};

// ═══════════════════════════════════════════════════════════════════
// Build CSR from a flat edge array via counting sort. O(V + E).
//
// The flat edge array is consumed (not modified). All output arrays
// are arena-allocated. The graph struct itself must be pre-allocated
// by the caller (arena or stack).
// ═══════════════════════════════════════════════════════════════════

inline void build_csr(
    fx::Alloc a,
    Arena& arena,
    TraceGraph* graph,
    const Edge* edges,
    uint32_t num_edges,
    uint32_t num_ops) {
  graph->num_edges = num_edges;

  // Degenerate case: empty trace.  alloc_array(0) returns nullptr, and
  // memcpy/memset on nullptr is UB even with n=0.  Bail early.
  if (num_ops == 0) return;

  // Allocate CSR arrays.
  graph->fwd_edges = arena.alloc_array<Edge>(a, num_edges);
  graph->fwd_offsets = arena.alloc_array<uint32_t>(a, num_ops + 1);
  graph->rev_edges = arena.alloc_array<Edge>(a, num_edges);
  graph->rev_offsets = arena.alloc_array<uint32_t>(a, num_ops + 1);

  // Count degrees.
  std::memset(graph->fwd_offsets, 0, (num_ops + 1) * sizeof(uint32_t));
  std::memset(graph->rev_offsets, 0, (num_ops + 1) * sizeof(uint32_t));

  for (uint32_t e = 0; e < num_edges; e++) {
    graph->fwd_offsets[edges[e].src.raw() + 1]++;
    graph->rev_offsets[edges[e].dst.raw() + 1]++;
  }

  // Prefix sum → offsets.
  for (uint32_t i = 1; i <= num_ops; i++) {
    graph->fwd_offsets[i] += graph->fwd_offsets[i - 1];
    graph->rev_offsets[i] += graph->rev_offsets[i - 1];
  }

  // Scatter edges into sorted positions (arena-allocated cursors).
  auto* fwd_cursor = arena.alloc_array<uint32_t>(a, num_ops);
  auto* rev_cursor = arena.alloc_array<uint32_t>(a, num_ops);
  std::memcpy(fwd_cursor, graph->fwd_offsets, num_ops * sizeof(uint32_t));
  std::memcpy(rev_cursor, graph->rev_offsets, num_ops * sizeof(uint32_t));

  for (uint32_t e = 0; e < num_edges; e++) {
    graph->fwd_edges[fwd_cursor[edges[e].src.raw()]++] = edges[e];
    graph->rev_edges[rev_cursor[edges[e].dst.raw()]++] = edges[e];
  }
}

} // namespace crucible
