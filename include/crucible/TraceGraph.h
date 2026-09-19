#pragma once

#include <cstdint>
#include <cstring>
#include <new>
#include <span>

#include <crucible/Arena.h>
#include <crucible/MerkleDag.h>
#include <crucible/fixy/Wrap.h>
#include <crucible/safety/_Decide.h>
#include <crucible/safety/_Post.h>
#include <crucible/safety/_Pre.h>

namespace crucible {

enum class EdgeKind : uint8_t {
    DATA_FLOW,  // a tensor the source produced and the destination consumes
    ALIAS,  // two ops naming one storage address, as a view or an in-place write
    CONTROL_FLOW,  // an ordering with no value passed along it
    SCALAR_FLOW,  // a scalar read out of a tensor and consumed as a scalar
};

// An edge is port-level: src_port selects an output of the source and
// dst_port selects an input of the destination.
struct Edge {
    // A port index is bounded by the operation arity accepted at the frontend
    // boundary, which is 64 inputs and 64 outputs.
    static constexpr uint8_t kMaxPort = 63;

    OpIndex src;
    OpIndex dst;
    uint8_t src_port = 0;
    uint8_t dst_port = 0;
    EdgeKind kind = EdgeKind::DATA_FLOW;
    uint8_t pad = 0;

    // The port fields stay plain bytes so the struct keeps its layout lock.
    // These setters carry the bound instead, so an assembly path that writes
    // a port through them cannot store an out-of-range index.
    void set_src_port(uint8_t p) noexcept pre(::crucible::decide::in_range<uint8_t>(p, 0, kMaxPort)) {
        src_port = p;
        CRUCIBLE_POST(0, src_port == p);
    }
    void set_dst_port(uint8_t p) noexcept pre(::crucible::decide::in_range<uint8_t>(p, 0, kMaxPort)) {
        dst_port = p;
        CRUCIBLE_POST(0, dst_port == p);
    }
};

static_assert(sizeof(Edge) == 12, "Edge must be 12 bytes");
CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE(Edge);

struct TraceGraph {
    using BuiltCount = crucible::fixy::wrap::WriteOnce<uint32_t>;

    // The ops in trace order.
    TraceEntry* ops = nullptr;
    BuiltCount num_ops;

    // Sorted by source, so a walk answers which ops consume a given output.
    Edge* fwd_edges = nullptr;
    uint32_t* fwd_offsets = nullptr;  // num_ops + 1 entries

    // Sorted by destination, so a walk answers which op produced a given input.
    Edge* rev_edges = nullptr;
    uint32_t* rev_offsets = nullptr;  // num_ops + 1 entries

    BuiltCount num_edges;

    TensorSlot* slots = nullptr;
    BuiltCount num_slots;  // count of distinct storages, not of tensors

    ContentHash content_hash;

    // The highest metadata-log index this trace reads. The owner of that log
    // may only advance its tail once every read here has finished, because
    // the reads point into the log rather than copying out of it.
    BuiltCount max_meta_end;
    uint32_t pad_tg = 0;

    // The zero-count guard is not redundant with the range check. With no ops
    // the subtraction below wraps to the largest uint32_t and the range check
    // then admits every index.
    [[nodiscard, gnu::pure]] const Edge* fwd_begin(OpIndex i) const noexcept CRUCIBLE_LIFETIMEBOUND {
        const uint32_t n_ops = num_ops.get_assuming_set();
        CRUCIBLE_PRE(n_ops > 0u);
        CRUCIBLE_PRE(::crucible::decide::in_range<std::uint32_t>(i.raw(), 0u, n_ops - 1u));
        return fwd_edges + fwd_offsets[i.raw()];
    }
    [[nodiscard, gnu::pure]] const Edge* fwd_end(OpIndex i) const noexcept CRUCIBLE_LIFETIMEBOUND {
        const uint32_t n_ops = num_ops.get_assuming_set();
        CRUCIBLE_PRE(n_ops > 0u);
        CRUCIBLE_PRE(::crucible::decide::in_range<std::uint32_t>(i.raw(), 0u, n_ops - 1u));
        return fwd_edges + fwd_offsets[i.raw() + 1];
    }
    [[nodiscard, gnu::pure]] uint32_t out_degree(OpIndex i) const noexcept {
        const uint32_t n_ops = num_ops.get_assuming_set();
        CRUCIBLE_PRE(n_ops > 0u);
        CRUCIBLE_PRE(::crucible::decide::in_range<std::uint32_t>(i.raw(), 0u, n_ops - 1u));
        return fwd_offsets[i.raw() + 1] - fwd_offsets[i.raw()];
    }

    [[nodiscard, gnu::pure]] const Edge* rev_begin(OpIndex i) const noexcept CRUCIBLE_LIFETIMEBOUND {
        const uint32_t n_ops = num_ops.get_assuming_set();
        CRUCIBLE_PRE(n_ops > 0u);
        CRUCIBLE_PRE(::crucible::decide::in_range<std::uint32_t>(i.raw(), 0u, n_ops - 1u));
        return rev_edges + rev_offsets[i.raw()];
    }
    [[nodiscard, gnu::pure]] const Edge* rev_end(OpIndex i) const noexcept CRUCIBLE_LIFETIMEBOUND {
        const uint32_t n_ops = num_ops.get_assuming_set();
        CRUCIBLE_PRE(n_ops > 0u);
        CRUCIBLE_PRE(::crucible::decide::in_range<std::uint32_t>(i.raw(), 0u, n_ops - 1u));
        return rev_edges + rev_offsets[i.raw() + 1];
    }
    [[nodiscard, gnu::pure]] uint32_t in_degree(OpIndex i) const noexcept {
        const uint32_t n_ops = num_ops.get_assuming_set();
        CRUCIBLE_PRE(n_ops > 0u);
        CRUCIBLE_PRE(::crucible::decide::in_range<std::uint32_t>(i.raw(), 0u, n_ops - 1u));
        return rev_offsets[i.raw() + 1] - rev_offsets[i.raw()];
    }

    [[nodiscard, gnu::pure]] const TraceEntry& op(OpIndex i) const noexcept CRUCIBLE_LIFETIMEBOUND {
        const uint32_t n_ops = num_ops.get_assuming_set();
        CRUCIBLE_PRE(n_ops > 0u);
        CRUCIBLE_PRE(::crucible::decide::in_range<std::uint32_t>(i.raw(), 0u, n_ops - 1u));
        return ops[i.raw()];
    }

    // The precondition refuses a graph with no ops, where the fold
    // legitimately produces a zero hash. A caller that tolerates that
    // sentinel branches on the op count first and reads the field directly.
    [[nodiscard]] ValidContentHash computed_content_hash() const noexcept
        pre(::crucible::decide::is_non_zero(content_hash)) {
        return ValidContentHash{content_hash};
    }
};

[[nodiscard]] inline TraceGraph* alloc_trace_graph(effects::Alloc a, Arena& arena) noexcept CRUCIBLE_LIFETIMEBOUND {
    return ::new(arena.alloc_obj<TraceGraph>(a)) TraceGraph{};
}

// Counting sort into both adjacency arrays, linear in ops plus edges. The
// caller owns the graph struct. Only the arrays inside it are allocated here.
inline void build_csr(effects::Alloc a, Arena& arena, TraceGraph* graph, const Edge* edges, uint32_t num_edges,
                      uint32_t num_ops) {
    graph->num_edges.set(num_edges);
    graph->num_ops.set(num_ops);

    // Allocating zero elements yields a null pointer, and passing null to
    // memset or memcpy is undefined even for a zero length.
    if (num_ops == 0) return;

    graph->fwd_edges = arena.alloc_array<Edge>(a, num_edges);
    graph->fwd_offsets = arena.alloc_array<uint32_t>(a, num_ops + 1);
    graph->rev_edges = arena.alloc_array<Edge>(a, num_edges);
    graph->rev_offsets = arena.alloc_array<uint32_t>(a, num_ops + 1);

    std::memset(graph->fwd_offsets, 0, (num_ops + 1) * sizeof(uint32_t));
    std::memset(graph->rev_offsets, 0, (num_ops + 1) * sizeof(uint32_t));

    for (uint32_t e = 0; e < num_edges; e++) {
        graph->fwd_offsets[edges[e].src.raw() + 1]++;
        graph->rev_offsets[edges[e].dst.raw() + 1]++;
    }

    for (uint32_t i = 1; i <= num_ops; i++) {
        graph->fwd_offsets[i] += graph->fwd_offsets[i - 1];
        graph->rev_offsets[i] += graph->rev_offsets[i - 1];
    }

    auto* fwd_cursor = arena.alloc_array<uint32_t>(a, num_ops);
    auto* rev_cursor = arena.alloc_array<uint32_t>(a, num_ops);
    std::memcpy(fwd_cursor, graph->fwd_offsets, num_ops * sizeof(uint32_t));
    std::memcpy(rev_cursor, graph->rev_offsets, num_ops * sizeof(uint32_t));

    for (uint32_t e = 0; e < num_edges; e++) {
        graph->fwd_edges[fwd_cursor[edges[e].src.raw()]++] = edges[e];
        graph->rev_edges[rev_cursor[edges[e].dst.raw()]++] = edges[e];
    }
}

}  // namespace crucible
