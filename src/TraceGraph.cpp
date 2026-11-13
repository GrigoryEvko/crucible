// The cold bodies of crucible/TraceGraph.h: the reads of the edges of an op,
// the allocation of a graph and the counting sort of its CSR arrays.  A unit
// that includes the header then compiles no part of them.

#include <crucible/TraceGraph.h>

namespace crucible {

::fixy::View<const Edge> TraceGraph::fwd_of(OpIndex i) const noexcept {
    return edges_of_(fwd_edges, fwd_offsets, checked_index_(i));
}

uint32_t TraceGraph::out_degree(OpIndex i) const noexcept {
    const uint32_t index = checked_index_(i);
    return offset_(fwd_offsets, index + 1) - offset_(fwd_offsets, index);
}

::fixy::View<const Edge> TraceGraph::rev_of(OpIndex i) const noexcept {
    return edges_of_(rev_edges, rev_offsets, checked_index_(i));
}

uint32_t TraceGraph::in_degree(OpIndex i) const noexcept {
    const uint32_t index = checked_index_(i);
    return offset_(rev_offsets, index + 1) - offset_(rev_offsets, index);
}

uint32_t TraceGraph::offset_(::fixy::View<const uint32_t> offsets, uint32_t index) noexcept {
    return *offsets.get(index).expect("an index of a CSR offset array is at most the op count");
}

::fixy::View<const Edge> TraceGraph::edges_of_(::fixy::View<const Edge> edges, ::fixy::View<const uint32_t> offsets,
                                               uint32_t index) noexcept {
    const uint32_t first = offset_(offsets, index);
    return edges.window(first, offset_(offsets, index + 1) - first)
        .expect("the offsets of an op bound a run of its CSR edges");
}

TraceGraph* alloc_trace_graph(::foundation::effects::Alloc a, ::fixy::BumpArena& arena) noexcept {
    return &*arena.make<TraceGraph>(a);
}

void build_csr(::foundation::effects::Alloc a, ::fixy::BumpArena& arena, TraceGraph* graph, const Edge* edges,
               uint32_t num_edges, uint32_t num_ops) {
    graph->num_edges.set(num_edges);
    graph->num_ops.set(num_ops);

    // A graph with no ops has no offset arrays.
    if (num_ops == 0) return;

    graph->fwd_edges = arena.array(a, num_edges, Edge{});
    graph->fwd_offsets = arena.array<uint32_t>(a, num_ops + 1, 0);
    graph->rev_edges = arena.array(a, num_edges, Edge{});
    graph->rev_offsets = arena.array<uint32_t>(a, num_ops + 1, 0);

    for (uint32_t e = 0; e < num_edges; e++) {
        ++detail::csr_element_(graph->fwd_offsets, edges[e].src.raw() + 1);
        ++detail::csr_element_(graph->rev_offsets, edges[e].dst.raw() + 1);
    }

    for (uint32_t i = 1; i <= num_ops; i++) {
        detail::csr_element_(graph->fwd_offsets, i) += detail::csr_element_(graph->fwd_offsets, i - 1);
        detail::csr_element_(graph->rev_offsets, i) += detail::csr_element_(graph->rev_offsets, i - 1);
    }

    // The cursor of each op starts at its offset.  The last offset is the
    // edge count, which no cursor needs.
    const ::fixy::View<uint32_t> fwd_cursor = arena.array<uint32_t>(a, num_ops, 0);
    const ::fixy::View<uint32_t> rev_cursor = arena.array<uint32_t>(a, num_ops, 0);
    ::fixy::copy(fwd_cursor, graph->fwd_offsets.window(0, num_ops).expect("the offsets hold one entry for each op"))
        .expect("the cursors hold one entry for each op");
    ::fixy::copy(rev_cursor, graph->rev_offsets.window(0, num_ops).expect("the offsets hold one entry for each op"))
        .expect("the cursors hold one entry for each op");

    for (uint32_t e = 0; e < num_edges; e++) {
        detail::csr_element_(graph->fwd_edges, detail::csr_element_(fwd_cursor, edges[e].src.raw())++) = edges[e];
        detail::csr_element_(graph->rev_edges, detail::csr_element_(rev_cursor, edges[e].dst.raw())++) = edges[e];
    }
}

}  // namespace crucible
