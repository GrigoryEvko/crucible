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

::fixy::Ref<TraceGraph> alloc_trace_graph(::foundation::effects::Alloc a, TraceScratch::Epoch& epoch) noexcept {
    return ::fixy::erase_brand(epoch.make<TraceGraph>(a));
}

void build_csr(::foundation::effects::Alloc a, TraceScratch::Epoch& epoch, TraceGraph& graph,
               ::fixy::View<const Edge> edges, uint32_t num_ops) {
    const uint32_t num_edges =
        ::fixy::narrow<uint32_t>(edges.size()).expect("the build of a trace gives at most 2^32 - 1 edges");
    graph.num_edges.set(num_edges);
    graph.num_ops.set(num_ops);

    // A graph with no ops has no offset arrays.
    if (num_ops == 0) return;

    const auto fwd_edges_run = epoch.array(a, num_edges, Edge{});
    const auto fwd_offsets_run = epoch.array<uint32_t>(a, num_ops + 1, 0);
    const auto rev_edges_run = epoch.array(a, num_edges, Edge{});
    const auto rev_offsets_run = epoch.array<uint32_t>(a, num_ops + 1, 0);
    // A plain View of an arena run: the View carries no brand of its arena.
    graph.fwd_edges = fwd_edges_run.view();
    // A plain View of an arena run: the View carries no brand of its arena.
    graph.fwd_offsets = fwd_offsets_run.view();
    // A plain View of an arena run: the View carries no brand of its arena.
    graph.rev_edges = rev_edges_run.view();
    // A plain View of an arena run: the View carries no brand of its arena.
    graph.rev_offsets = rev_offsets_run.view();

    for (const Edge& edge : edges) {
        ++detail::csr_element_(graph.fwd_offsets, edge.src.raw() + 1);
        ++detail::csr_element_(graph.rev_offsets, edge.dst.raw() + 1);
    }

    for (uint32_t i = 1; i <= num_ops; i++) {
        detail::csr_element_(graph.fwd_offsets, i) += detail::csr_element_(graph.fwd_offsets, i - 1);
        detail::csr_element_(graph.rev_offsets, i) += detail::csr_element_(graph.rev_offsets, i - 1);
    }

    // The cursor of each op starts at its offset.  The last offset is the
    // edge count, which no cursor needs.
    const auto fwd_cursor_run = epoch.array<uint32_t>(a, num_ops, 0);
    const auto rev_cursor_run = epoch.array<uint32_t>(a, num_ops, 0);
    // A plain View of an arena run: the View carries no brand of its arena.
    const ::fixy::View<uint32_t> fwd_cursor = fwd_cursor_run.view();
    // A plain View of an arena run: the View carries no brand of its arena.
    const ::fixy::View<uint32_t> rev_cursor = rev_cursor_run.view();
    ::fixy::copy(fwd_cursor, graph.fwd_offsets.window(0, num_ops).expect("the offsets hold one entry for each op"))
        .expect("the cursors hold one entry for each op");
    ::fixy::copy(rev_cursor, graph.rev_offsets.window(0, num_ops).expect("the offsets hold one entry for each op"))
        .expect("the cursors hold one entry for each op");

    for (const Edge& edge : edges) {
        detail::csr_element_(graph.fwd_edges, detail::csr_element_(fwd_cursor, edge.src.raw())++) = edge;
        detail::csr_element_(graph.rev_edges, detail::csr_element_(rev_cursor, edge.dst.raw())++) = edge;
    }
}

}  // namespace crucible
