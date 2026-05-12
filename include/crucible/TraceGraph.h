#pragma once

#include <cstdint>
#include <cstring>
#include <new>
#include <span>

#include <crucible/Arena.h>
#include <crucible/MerkleDag.h>
#include <crucible/safety/Decide.h>
#include <crucible/safety/Mutation.h>
#include <crucible/safety/Post.h>
#include <crucible/safety/Pre.h>

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
//
// Port bounds: CDAG_MAX_INPUTS = CDAG_MAX_OUTPUTS = 64 (validated at
// the Vessel FFI boundary).  A port index ≥ 64 is categorically a
// bug.  The fields stay uint8_t (uint8_t max == 255, headroom vs
// the real 64-cap); a set_port() helper with pre() makes the
// invariant load-bearing at mutation sites without wrapping the
// field type (Edge is trivially-copyable POD by layout lock).
struct Edge {
  // Ports are bounded above by CDAG_MAX_INPUTS / CDAG_MAX_OUTPUTS
  // (both 64); 63 is the inclusive upper endpoint.  Use the strong
  // typedef at call sites that MUST prove bounds.
  static constexpr uint8_t kMaxPort = 63;

  OpIndex src;            // 4B — source op index (default = none)
  OpIndex dst;            // 4B — destination op index (default = none)
  uint8_t src_port = 0;  // 1B — output index of src, ≤ kMaxPort
  uint8_t dst_port = 0;  // 1B — input index of dst,  ≤ kMaxPort
  EdgeKind kind = EdgeKind::DATA_FLOW; // 1B
  uint8_t pad = 0;       // 1B

  // Validating setters: fire on port ≥ 64.  The binary-search
  // edge assembly paths that populate Edge pairs should route
  // through these rather than writing the fields directly.
  // CONTRACT-Edge-SetSrcPort-PRE / CONTRACT-Edge-SetDstPort-PRE
  // (cite migration 2026-05-08): the bare `p <= kMaxPort` clause
  // is promoted to `decide::in_range<uint8_t>(p, 0, kMaxPort)`.
  // Parameter-only predicate (no `this->`-member access) — vanilla
  // P2900 `pre()` is consteval-safe; the win is the named
  // predicate so future hardening (lifting `p` callers to
  // `Refined<bounded_above<kMaxPort>, uint8_t>`) propagates by
  // name, and `grep decide::in_range` discovers this gate.
  void set_src_port(uint8_t p) noexcept
      pre (::crucible::decide::in_range<uint8_t>(p, 0, kMaxPort))
  {
    src_port = p;
    // CONTRACT-Edge-SetSrcPort-POST: state-mutation post.  Pre rules
    // out p > kMaxPort, leaving `src_port = p` as the only legal
    // mutation; post catches a refactor that publishes a different
    // value (e.g. accidental `src_port = dst_port` typo, or an
    // out-of-scope local shadow).  Sibling of
    // CONTRACT-WriteOnceNonNull-Set-POST (commit 98d0ff8) and
    // CONTRACT-Graph-SetInputSlots-POST (commit d38089c) — same
    // "after assignment, the field equals the input" framing.
    // Edge is a non-template POD struct; set_src_port is a non-
    // template member — single instantiation per libcrucible TU,
    // no function-template POST poison risk (cf. Pre.h §"Function-
    // template POST poison" trap, commit 9e818c7).
    CRUCIBLE_POST(0, src_port == p);
  }
  void set_dst_port(uint8_t p) noexcept
      pre (::crucible::decide::in_range<uint8_t>(p, 0, kMaxPort))
  {
    dst_port = p;
    // CONTRACT-Edge-SetDstPort-POST: mirror of
    // CONTRACT-Edge-SetSrcPort-POST above.  Catches the same
    // accidental-shadow-or-typo class on the dst side.
    CRUCIBLE_POST(0, dst_port == p);
  }
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
  using BuiltCount = crucible::safety::WriteOnce<uint32_t>;

  // Nodes (ops in trace order).
  TraceEntry* ops = nullptr;
  BuiltCount num_ops;

  // Forward CSR: edges sorted by src.
  Edge* fwd_edges = nullptr;
  uint32_t* fwd_offsets = nullptr; // num_ops + 1 entries

  // Reverse CSR: edges sorted by dst.
  Edge* rev_edges = nullptr;
  uint32_t* rev_offsets = nullptr; // num_ops + 1 entries

  BuiltCount num_edges;

  // Liveness analysis results (populated by build_trace Phase 3).
  TensorSlot* slots = nullptr;    // arena-allocated array of all tensor slots
  BuiltCount num_slots;           // total unique storages identified

  // Fused content hash — computed during build_trace as a streaming
  // accumulator, avoiding a redundant second pass over all ops.
  ContentHash content_hash;       // 8B

  // Maximum MetaLog index consumed by this trace. Caller advances
  // MetaLog tail AFTER all meta reads are complete (zero-copy safety).
  BuiltCount max_meta_end;
  uint32_t pad_tg = 0;            // 4B — alignment

  // ── Forward queries (src → dst): "who consumes op i's outputs?" ──
  // gnu::pure: accessors read *this fields + the CSR arrays; no side
  // effects, no memory writes.  Optimizer may CSE across successive
  // calls with the same argument within a basic block.
  //
  // TypeSafe: OpIndex parameter rejects accidental SlotId / NodeId /
  // raw uint32_t mixing.  The bounds gate discharges through the
  // named predicate `crucible::decide::in_range` (CONTRACT-102):
  // closed interval `[0, num_ops - 1]` is reviewable as a single
  // citation rather than a bare `<` (which conflates exclusive
  // count with inclusive max — see decide.h anti-patterns).
  //
  // The `num_ops > 0` companion guard is paired because
  // `num_ops - 1u` underflows to UINT32_MAX when num_ops==0, which
  // would make `in_range(i, 0, UINT32_MAX)` accept every value;
  // production never calls these accessors on an empty graph (no
  // valid OpIndex can exist for a zero-op TraceGraph) but defense-
  // in-depth catches a future refactor that exposes this path.
  //
  // The pre clauses move from P2900 `pre()` to in-body
  // CRUCIBLE_PRE because P2900 `pre()` referencing a class member
  // through `this->` (`num_ops`) is silently bypassed at consteval
  // in GCC 16.1.1 (same gotcha that forced CONTRACT-100 /
  // CONTRACT-101 to migrate).  CRUCIBLE_PRE fires symmetrically at
  // consteval, runtime, and as `[[assume]]` for the optimizer.
  [[nodiscard, gnu::pure]] const Edge* fwd_begin(OpIndex i) const noexcept CRUCIBLE_LIFETIMEBOUND
  {
    const uint32_t n_ops = num_ops.get_assuming_set();
    CRUCIBLE_PRE(n_ops > 0u);
    CRUCIBLE_PRE(::crucible::decide::in_range<std::uint32_t>(
        i.raw(), 0u, n_ops - 1u));
    return fwd_edges + fwd_offsets[i.raw()];
  }
  [[nodiscard, gnu::pure]] const Edge* fwd_end(OpIndex i) const noexcept CRUCIBLE_LIFETIMEBOUND
  {
    const uint32_t n_ops = num_ops.get_assuming_set();
    CRUCIBLE_PRE(n_ops > 0u);
    CRUCIBLE_PRE(::crucible::decide::in_range<std::uint32_t>(
        i.raw(), 0u, n_ops - 1u));
    return fwd_edges + fwd_offsets[i.raw() + 1];
  }
  [[nodiscard, gnu::pure]] uint32_t out_degree(OpIndex i) const noexcept
  {
    const uint32_t n_ops = num_ops.get_assuming_set();
    CRUCIBLE_PRE(n_ops > 0u);
    CRUCIBLE_PRE(::crucible::decide::in_range<std::uint32_t>(
        i.raw(), 0u, n_ops - 1u));
    return fwd_offsets[i.raw() + 1] - fwd_offsets[i.raw()];
  }

  // ── Reverse queries (dst → src): "who produces op i's inputs?" ──
  [[nodiscard, gnu::pure]] const Edge* rev_begin(OpIndex i) const noexcept CRUCIBLE_LIFETIMEBOUND
  {
    const uint32_t n_ops = num_ops.get_assuming_set();
    CRUCIBLE_PRE(n_ops > 0u);
    CRUCIBLE_PRE(::crucible::decide::in_range<std::uint32_t>(
        i.raw(), 0u, n_ops - 1u));
    return rev_edges + rev_offsets[i.raw()];
  }
  [[nodiscard, gnu::pure]] const Edge* rev_end(OpIndex i) const noexcept CRUCIBLE_LIFETIMEBOUND
  {
    const uint32_t n_ops = num_ops.get_assuming_set();
    CRUCIBLE_PRE(n_ops > 0u);
    CRUCIBLE_PRE(::crucible::decide::in_range<std::uint32_t>(
        i.raw(), 0u, n_ops - 1u));
    return rev_edges + rev_offsets[i.raw() + 1];
  }
  [[nodiscard, gnu::pure]] uint32_t in_degree(OpIndex i) const noexcept
  {
    const uint32_t n_ops = num_ops.get_assuming_set();
    CRUCIBLE_PRE(n_ops > 0u);
    CRUCIBLE_PRE(::crucible::decide::in_range<std::uint32_t>(
        i.raw(), 0u, n_ops - 1u));
    return rev_offsets[i.raw() + 1] - rev_offsets[i.raw()];
  }

  // ── Node access ──
  [[nodiscard, gnu::pure]] const TraceEntry& op(OpIndex i) const noexcept CRUCIBLE_LIFETIMEBOUND
  {
    const uint32_t n_ops = num_ops.get_assuming_set();
    CRUCIBLE_PRE(n_ops > 0u);
    CRUCIBLE_PRE(::crucible::decide::in_range<std::uint32_t>(
        i.raw(), 0u, n_ops - 1u));
    return ops[i.raw()];
  }

  // ── Content hash (post-build) ──
  //
  // PROD-WRAP-6 (#535) / WRAP-TraceGraph-6 (#1047) accessor: lift the
  // raw `content_hash` field to `Refined<non_zero, ContentHash>` for
  // callers that require the non-zero invariant typed at the boundary.
  // Mirror of the sibling accessors on RegionNode and LoopNode (see
  // RegionNode::computed_content_hash, LoopNode::computed_body_content_hash).
  //
  // Lifecycle: TraceGraph is default-constructed with content_hash == 0
  // and the build_trace fold streams a non-zero hash into the field
  // before consumers (BackgroundThread.h:386 / 1142 forward to
  // make_region's precomputed_hash overload — which already gates on
  // `decide::is_non_zero(precomputed_hash) || num_ops == 0`).  This
  // accessor lets the producer typecheck at the build boundary instead
  // of the consumer.
  //
  // The accessor's pre-clause refuses the degenerate empty-graph case
  // (num_ops == 0, content_hash == 0 — the streaming fold legitimately
  // produces zero on an empty op list).  Callers that legitimately
  // tolerate the zero-hash sentinel should branch on num_ops first or
  // read the raw `content_hash` field.
  //
  // CONTRACT-106 cite — non-zero hash sentinel via decide::is_non_zero
  // (CONTRACT-072 catalog).
  [[nodiscard]] crucible::safety::Refined<
      crucible::safety::non_zero, ContentHash>
  computed_content_hash() const noexcept
      pre (::crucible::decide::is_non_zero(content_hash))
  {
    return crucible::safety::Refined<
        crucible::safety::non_zero, ContentHash>{content_hash};
  }
};

[[nodiscard]] inline TraceGraph* alloc_trace_graph(
    effects::Alloc a, Arena& arena) noexcept CRUCIBLE_LIFETIMEBOUND
{
  return ::new (arena.alloc_obj<TraceGraph>(a)) TraceGraph{};
}

// ═══════════════════════════════════════════════════════════════════
// Build CSR from a flat edge array via counting sort. O(V + E).
//
// The flat edge array is consumed (not modified). All output arrays
// are arena-allocated. The graph struct itself must be pre-allocated
// by the caller (arena or stack).
// ═══════════════════════════════════════════════════════════════════

inline void build_csr(
    effects::Alloc a,
    Arena& arena,
    TraceGraph* graph,
    const Edge* edges,
    uint32_t num_edges,
    uint32_t num_ops) {
  graph->num_edges.set(num_edges);
  graph->num_ops.set(num_ops);

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
