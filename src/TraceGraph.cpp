// The cold bodies of crucible/TraceGraph.h: the reads of the edges of an op,
// and the build of a graph with the counting sort of its CSR arrays.  A unit
// that includes the header then compiles no part of them.

#include <crucible/TraceGraph.h>

namespace crucible {

::fixy::View<const Edge> TraceGraph::fwd_of(OpIndex i) const noexcept {
    return edges_of_(fwd_edges.view(), fwd_offsets.view(), checked_index_(i));
}

uint32_t TraceGraph::out_degree(OpIndex i) const noexcept {
    const uint32_t index = checked_index_(i);
    return offset_(fwd_offsets.view(), index + 1) - offset_(fwd_offsets.view(), index);
}

::fixy::View<const Edge> TraceGraph::rev_of(OpIndex i) const noexcept {
    return edges_of_(rev_edges.view(), rev_offsets.view(), checked_index_(i));
}

uint32_t TraceGraph::in_degree(OpIndex i) const noexcept {
    const uint32_t index = checked_index_(i);
    return offset_(rev_offsets.view(), index + 1) - offset_(rev_offsets.view(), index);
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

TraceGraphRef build_trace_graph(::foundation::effects::Alloc a, TraceScratch::Epoch& epoch, TraceGraphShape shape,
                                ::fixy::View<const Edge> edges) {
    const uint32_t num_edges =
        ::fixy::narrow<uint32_t>(edges.size()).expect("the build of a trace gives at most 2^32 - 1 edges");
    const uint32_t num_ops = shape.num_ops;
    const TraceGraphRef made = epoch.make<TraceGraph>(a, TraceGraph{.num_ops = num_ops,
                                                                    .num_edges = num_edges,
                                                                    .num_slots = shape.num_slots,
                                                                    .max_meta_end = shape.max_meta_end});
    TraceGraph& graph = *made;

    // A graph with no ops has no offset arrays.
    if (num_ops == 0) return made;

    graph.fwd_edges = epoch.array(a, num_edges, Edge{});
    graph.fwd_offsets = epoch.array<uint32_t>(a, num_ops + 1, 0);
    graph.rev_edges = epoch.array(a, num_edges, Edge{});
    graph.rev_offsets = epoch.array<uint32_t>(a, num_ops + 1, 0);
    // The counting sort writes the runs of the graph through their Views.
    const ::fixy::View<Edge> fwd_edges = graph.fwd_edges.view();
    const ::fixy::View<uint32_t> fwd_offsets = graph.fwd_offsets.view();
    const ::fixy::View<Edge> rev_edges = graph.rev_edges.view();
    const ::fixy::View<uint32_t> rev_offsets = graph.rev_offsets.view();

    for (const Edge& edge : edges) {
        ++detail::csr_element_(fwd_offsets, edge.src.raw() + 1);
        ++detail::csr_element_(rev_offsets, edge.dst.raw() + 1);
    }

    for (uint32_t i = 1; i <= num_ops; i++) {
        detail::csr_element_(fwd_offsets, i) += detail::csr_element_(fwd_offsets, i - 1);
        detail::csr_element_(rev_offsets, i) += detail::csr_element_(rev_offsets, i - 1);
    }

    // The cursor of each op starts at its offset.  The last offset is the
    // edge count, which no cursor needs.
    auto fwd_cursor_run = epoch.array<uint32_t>(a, num_ops, 0);
    auto rev_cursor_run = epoch.array<uint32_t>(a, num_ops, 0);
    const ::fixy::View<uint32_t> fwd_cursor = fwd_cursor_run.view();
    const ::fixy::View<uint32_t> rev_cursor = rev_cursor_run.view();
    ::fixy::copy(fwd_cursor, fwd_offsets.window(0, num_ops).expect("the offsets hold one entry for each op"))
        .expect("the cursors hold one entry for each op");
    ::fixy::copy(rev_cursor, rev_offsets.window(0, num_ops).expect("the offsets hold one entry for each op"))
        .expect("the cursors hold one entry for each op");

    for (const Edge& edge : edges) {
        detail::csr_element_(fwd_edges, detail::csr_element_(fwd_cursor, edge.src.raw())++) = edge;
        detail::csr_element_(rev_edges, detail::csr_element_(rev_cursor, edge.dst.raw())++) = edge;
    }
    return made;
}

}  // namespace crucible
