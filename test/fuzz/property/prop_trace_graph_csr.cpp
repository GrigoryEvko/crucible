// ═══════════════════════════════════════════════════════════════════
// prop_trace_graph_csr.cpp — structural-correctness fuzzer for
// build_csr (TraceGraph.h, L6 Graphs).
//
// build_csr turns a flat Edge[] into a bidirectional CSR (forward,
// sorted by src; reverse, sorted by dst) via counting sort, O(V+E),
// once per recorded iteration on the background thread.  Fusion,
// scheduling, and buffer allocation all index into that CSR — a
// prefix-sum off-by-one or a scatter-cursor bug silently corrupts the
// whole dataflow graph (wrong producer/consumer edges → wrong fusion
// groups → wrong memory plan).  No fuzzer covered it.
//
// Per random edge set, asserts:
//   (A) Offset shape: fwd_offsets/rev_offsets have num_ops+1 entries
//       with offsets[0]==0, offsets[num_ops]==num_edges, and are
//       monotonically non-decreasing (so every per-op slice is valid).
//   (B) Bucketing + stability + preservation: build_csr is a STABLE
//       counting sort, so the forward CSR must equal the input edges
//       stable-sorted by src, and the reverse CSR stable-sorted by dst.
//       The oracle uses std::stable_sort over edge INDICES — an
//       independent algorithm from the counting sort under test — and
//       compares field-by-field.  This single check subsumes:
//         * every fwd edge in op v's slice has src==v (bucketing),
//         * within a slice, input order is preserved (stability),
//         * the CSR edge multiset equals the input (preservation).
//   (C) Degree consistency: out_degree(v)=fwd_offsets[v+1]-fwd_offsets[v]
//       equals the number of input edges with src==v (and in_degree/dst).
//   (D) Determinism (DetSafe): the same edge set built twice on two
//       fresh arenas yields byte-identical offsets and edge order.
//
// build_csr takes a plain Edge[] + counts (no recorded-trace
// dependency), so it fuzzes in isolation like compute_memory_plan.
// ═══════════════════════════════════════════════════════════════════

#include "property_runner.h"

#include <crucible/TraceGraph.h>
#include <fixy/Core.h>
#include <foundation/effects/Effect.h>

#include <cstdint>

namespace {

// Bounds: up to 64 ops over up to 256 edges keeps each iteration cheap
// under ASan (O(V·E) oracle order + O(E) compares) while exercising
// dense multi-edge buckets, self-loops, and empty-edge graphs.
inline constexpr uint32_t kMaxOps = 64;
inline constexpr uint32_t kMaxEdges = 256;

struct EdgeSpec {
    uint32_t src = 0;  // in [0, num_ops)
    uint32_t dst = 0;  // in [0, num_ops)
    uint8_t src_port = 0;  // in [0, 63]
    uint8_t dst_port = 0;  // in [0, 63]
    ::fixy::FixedArray<uint8_t, 2> pad{};
};

struct GraphSpec {
    ::fixy::FixedArray<EdgeSpec, kMaxEdges> edges{};
    uint32_t num_edges = 0;
    uint32_t num_ops = 0;  // >= 1
};

// The edge spec at position e of a spec.  Each position that the test
// reads is below the edge count, which is at most kMaxEdges.
[[nodiscard]] const EdgeSpec& edge_spec_at(const GraphSpec& spec CRUCIBLE_LIFETIMEBOUND, uint32_t e) noexcept {
    return spec.edges.at(::fixy::Index<kMaxEdges>::check(e).expect("a position of a spec is below kMaxEdges"));
}
template <class... Rest>
void edge_spec_at(const GraphSpec&&, Rest&&...) =
    delete("the reference names an edge of the spec, and a temporary ends with the full expression");

// The count at op v of a per-op count array.  Each op is below kMaxOps.
[[nodiscard]] uint32_t count_at(const ::fixy::FixedArray<uint32_t, kMaxOps>& counts, uint32_t v) noexcept {
    return counts.at(::fixy::Index<kMaxOps>::check(v).expect("an op of a spec is below kMaxOps"));
}

// Adds one to the count at op v of a per-op count array.  Each op is below
// kMaxOps.
void increment_count_at(::fixy::FixedArray<uint32_t, kMaxOps>& counts, uint32_t v) noexcept {
    counts.with_element(::fixy::Index<kMaxOps>::check(v).expect("an op of a spec is below kMaxOps"),
                        [](uint32_t& count) noexcept { ++count; });
}

using EdgeOrder = ::fixy::FixedArray<uint32_t, kMaxEdges>;

// The stable order of the edge positions by a key of each edge: each key
// from 0 to num_ops - 1 in turn, and for each key the positions in input
// order.  That is the order of a stable sort of the positions by the key,
// built by a scan with no sort and no count, apart from the counting sort
// of build_csr.  Each key is below num_ops, so the order holds each
// position one time.  Complexity: O(num_ops * num_edges).
template <class KeyOf>
[[nodiscard]] EdgeOrder stable_order_by(const GraphSpec& spec, KeyOf const& key_of) noexcept {
    EdgeOrder order{};
    uint32_t filled = 0;
    for (uint32_t key = 0; key < spec.num_ops; ++key) {
        for (uint32_t e = 0; e < spec.num_edges; ++e) {
            if (key_of(edge_spec_at(spec, e)) != key) continue;
            order.with_element(::fixy::Index<kMaxEdges>::check(filled).expect("the order holds each edge one time"),
                               [e](uint32_t& position) noexcept { position = e; });
            ++filled;
        }
    }
    return order;
}

// The position at place k of an edge order.
[[nodiscard]] uint32_t position_at(const EdgeOrder& order, uint32_t k) noexcept {
    return order.at(::fixy::Index<kMaxEdges>::check(k).expect("a place of an order is below kMaxEdges"));
}

// Materialize the spec into the first edges of out, and give that run (kind
// defaults to DATA_FLOW; bucketing depends only on src/dst, so a constant
// kind is fine — the src/dst/port variation already distinguishes edges for
// the oracle).
::fixy::View<const crucible::Edge> materialize(const GraphSpec& spec, ::fixy::View<crucible::Edge> out) noexcept {
    using crucible::Edge;
    using crucible::OpIndex;
    const ::fixy::View<Edge> run = out.window(0, spec.num_edges).expect("a spec holds at most kMaxEdges edges");
    uint32_t e = 0;
    for (Edge& edge : run) {
        const EdgeSpec& edge_spec = edge_spec_at(spec, e);
        edge = Edge{};
        edge.src = OpIndex{edge_spec.src};
        edge.dst = OpIndex{edge_spec.dst};
        edge.src_port = edge_spec.src_port;
        edge.dst_port = edge_spec.dst_port;
        ++e;
    }
    return run;
}

[[nodiscard]] bool edge_eq(const crucible::Edge& a, const crucible::Edge& b) noexcept {
    return a.src.raw() == b.src.raw() && a.dst.raw() == b.dst.raw() && a.src_port == b.src_port
        && a.dst_port == b.dst_port && a.kind == b.kind;
}

// The offset at index of a CSR offset array, which holds num_ops + 1 entries.
[[nodiscard]] uint32_t offset_at(::fixy::View<const uint32_t> offsets, uint32_t index) noexcept {
    return *offsets.get(index).expect("the index is at most the op count");
}

// The edge at position k of a CSR edge array.
[[nodiscard]] const crucible::Edge& edge_at(::fixy::View<const crucible::Edge> edges, uint32_t k) noexcept {
    return *edges.get(k).expect("the position is below the edge count");
}

}  // namespace

int crucible::fuzz::prop::run_property(::fixy::os::Args const arguments) {
    using namespace crucible::fuzz::prop;
    using crucible::Edge;
    using crucible::OpIndex;
    using crucible::TraceGraph;
    using crucible::build_csr;
    using fixy::BumpArena;

    const Config cfg = parse_args(arguments, 20000);  // O(V·E) per iter

    return run(
        "trace_graph_csr", cfg,
        // ── Generator: random edge set over [0, num_ops) ──
        [](Rng& rng) noexcept -> GraphSpec {
            GraphSpec spec{};
            spec.num_ops = 1u + rng.next_below(kMaxOps);  // [1, kMaxOps]
            spec.num_edges = rng.next_below(kMaxEdges + 1u);  // [0, kMaxEdges]
            for (uint32_t e = 0; e < spec.num_edges; ++e) {
                spec.edges.with_element(
                    ::fixy::Index<kMaxEdges>::check(e).expect("a position of a spec is below kMaxEdges"),
                    [&rng, num_ops = spec.num_ops](EdgeSpec& edge_spec) noexcept {
                        edge_spec.src = rng.next_below(num_ops);
                        edge_spec.dst = rng.next_below(num_ops);
                        edge_spec.src_port = static_cast<uint8_t>(rng.next_below(64));
                        edge_spec.dst_port = static_cast<uint8_t>(rng.next_below(64));
                    });
            }
            return spec;
        },
        // ── Property: build the CSR, check (A)-(D) ──
        [](const GraphSpec& spec) noexcept -> bool {
            const uint32_t num_ops = spec.num_ops;
            const uint32_t num_edges = spec.num_edges;

            ::fixy::FixedArray<Edge, kMaxEdges> in_edges{};
            in_edges.with_mut([&spec](auto run) noexcept { static_cast<void>(materialize(spec, run.view())); });
            const ::fixy::View<const Edge> in_run =
                in_edges.view().window(0, num_edges).expect("a spec holds at most kMaxEdges edges");

            auto test = ::foundation::effects::testing::test();
            BumpArena arena{1 << 16};
            TraceGraph graph{};
            build_csr(test.alloc, arena, graph, in_run, num_ops);

            // (A) Offset shape — forward and reverse.
            const ::fixy::View<const uint32_t> fwd = graph.fwd_offsets;
            const ::fixy::View<const uint32_t> rev = graph.rev_offsets;
            if (fwd.size() != num_ops + 1u || rev.size() != num_ops + 1u) return false;
            if (offset_at(fwd, 0) != 0u || offset_at(rev, 0) != 0u) return false;
            if (offset_at(fwd, num_ops) != num_edges || offset_at(rev, num_ops) != num_edges) return false;
            for (uint32_t v = 0; v < num_ops; ++v) {
                if (offset_at(fwd, v + 1) < offset_at(fwd, v) || offset_at(rev, v + 1) < offset_at(rev, v))
                    return false;
            }

            // (C) Degree consistency — count input edges per src / dst.
            ::fixy::FixedArray<uint32_t, kMaxOps> src_count{};
            ::fixy::FixedArray<uint32_t, kMaxOps> dst_count{};
            for (uint32_t e = 0; e < num_edges; ++e) {
                increment_count_at(src_count, edge_spec_at(spec, e).src);
                increment_count_at(dst_count, edge_spec_at(spec, e).dst);
            }
            for (uint32_t v = 0; v < num_ops; ++v) {
                if (offset_at(fwd, v + 1) - offset_at(fwd, v) != count_at(src_count, v)) return false;
                if (offset_at(rev, v + 1) - offset_at(rev, v) != count_at(dst_count, v)) return false;
            }

            // (B) Bucketing + stability + preservation via an INDEPENDENT
            //     stable order of the edge indices (vs build_csr's counting
            //     sort).  Forward = stable-by-src, reverse = stable-by-dst.
            if (num_edges > 0) {
                // Forward: the stable order of the indices by src.
                const EdgeOrder by_src = stable_order_by(spec, [](const EdgeSpec& edge) noexcept { return edge.src; });
                for (uint32_t k = 0; k < num_edges; ++k) {
                    if (!edge_eq(edge_at(graph.fwd_edges, k), edge_at(in_run, position_at(by_src, k)))) return false;
                }

                // Reverse: the stable order of the indices by dst.
                const EdgeOrder by_dst = stable_order_by(spec, [](const EdgeSpec& edge) noexcept { return edge.dst; });
                for (uint32_t k = 0; k < num_edges; ++k) {
                    if (!edge_eq(edge_at(graph.rev_edges, k), edge_at(in_run, position_at(by_dst, k)))) return false;
                }
            }

            // (D) Determinism — rebuild on a fresh arena, compare bytes.
            ::fixy::FixedArray<Edge, kMaxEdges> in_edges2{};
            in_edges2.with_mut([&spec](auto run) noexcept { static_cast<void>(materialize(spec, run.view())); });
            const ::fixy::View<const Edge> in_run2 =
                in_edges2.view().window(0, num_edges).expect("a spec holds at most kMaxEdges edges");
            BumpArena arena2{1 << 16};
            TraceGraph graph2{};
            build_csr(test.alloc, arena2, graph2, in_run2, num_ops);
            for (uint32_t v = 0; v <= num_ops; ++v) {
                if (offset_at(graph2.fwd_offsets, v) != offset_at(fwd, v)) return false;
                if (offset_at(graph2.rev_offsets, v) != offset_at(rev, v)) return false;
            }
            for (uint32_t k = 0; k < num_edges; ++k) {
                if (!edge_eq(edge_at(graph2.fwd_edges, k), edge_at(graph.fwd_edges, k))) return false;
                if (!edge_eq(edge_at(graph2.rev_edges, k), edge_at(graph.rev_edges, k))) return false;
            }

            return true;
        });
}
