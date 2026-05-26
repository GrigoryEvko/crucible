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

#include <crucible/Arena.h>
#include <crucible/TraceGraph.h>
#include <crucible/effects/Capabilities.h>

#include <algorithm>
#include <array>
#include <cstdint>

namespace {

// Bounds: up to 64 ops over up to 256 edges keeps each iteration cheap
// under ASan (O(E log E) oracle sort + O(E) compares) while exercising
// dense multi-edge buckets, self-loops, and empty-edge graphs.
inline constexpr uint32_t kMaxOps = 64;
inline constexpr uint32_t kMaxEdges = 256;

struct EdgeSpec {
    uint32_t src = 0;       // in [0, num_ops)
    uint32_t dst = 0;       // in [0, num_ops)
    uint8_t src_port = 0;   // in [0, 63]
    uint8_t dst_port = 0;   // in [0, 63]
    uint8_t pad[2]{};
};

struct GraphSpec {
    std::array<EdgeSpec, kMaxEdges> edges{};
    uint32_t num_edges = 0;
    uint32_t num_ops = 0;   // >= 1
};

// Materialize the spec into a fresh Edge[] (kind defaults to DATA_FLOW;
// bucketing depends only on src/dst, so a constant kind is fine — the
// src/dst/port variation already distinguishes edges for the oracle).
void materialize(const GraphSpec& spec, crucible::Edge* out) noexcept {
    using crucible::Edge;
    using crucible::OpIndex;
    for (uint32_t e = 0; e < spec.num_edges; ++e) {
        out[e] = Edge{};
        out[e].src = OpIndex{spec.edges[e].src};
        out[e].dst = OpIndex{spec.edges[e].dst};
        out[e].src_port = spec.edges[e].src_port;
        out[e].dst_port = spec.edges[e].dst_port;
    }
}

[[nodiscard]] bool edge_eq(const crucible::Edge& a, const crucible::Edge& b) noexcept {
    return a.src.raw() == b.src.raw() && a.dst.raw() == b.dst.raw() &&
           a.src_port == b.src_port && a.dst_port == b.dst_port &&
           a.kind == b.kind;
}

}  // namespace

int main(int argc, char** argv) {
    using namespace crucible::fuzz::prop;
    using crucible::Edge;
    using crucible::OpIndex;
    using crucible::TraceGraph;
    using crucible::Arena;
    using crucible::build_csr;

    Config cfg = parse_args(argc, argv);
    if (cfg.iterations > 20'000) cfg.iterations = 20'000;  // O(E log E) per iter

    return run("trace_graph_csr", cfg,
        // ── Generator: random edge set over [0, num_ops) ──
        [](Rng& rng) noexcept -> GraphSpec {
            GraphSpec spec{};
            spec.num_ops = 1u + rng.next_below(kMaxOps);          // [1, kMaxOps]
            spec.num_edges = rng.next_below(kMaxEdges + 1u);      // [0, kMaxEdges]
            for (uint32_t e = 0; e < spec.num_edges; ++e) {
                spec.edges[e].src = rng.next_below(spec.num_ops);
                spec.edges[e].dst = rng.next_below(spec.num_ops);
                spec.edges[e].src_port = static_cast<uint8_t>(rng.next_below(64));
                spec.edges[e].dst_port = static_cast<uint8_t>(rng.next_below(64));
            }
            return spec;
        },
        // ── Property: build the CSR, check (A)-(D) ──
        [](const GraphSpec& spec) noexcept -> bool {
            const uint32_t num_ops = spec.num_ops;
            const uint32_t num_edges = spec.num_edges;

            Edge in_edges[kMaxEdges]{};
            materialize(spec, in_edges);

            auto test = crucible::effects::testing::test();
            Arena arena{1 << 16};
            TraceGraph graph{};
            build_csr(test.alloc, arena, &graph, in_edges, num_edges, num_ops);

            // (A) Offset shape — forward and reverse.
            const uint32_t* fwd = graph.fwd_offsets;
            const uint32_t* rev = graph.rev_offsets;
            if (fwd == nullptr || rev == nullptr) return false;
            if (fwd[0] != 0u || rev[0] != 0u) return false;
            if (fwd[num_ops] != num_edges || rev[num_ops] != num_edges) return false;
            for (uint32_t v = 0; v < num_ops; ++v) {
                if (fwd[v + 1] < fwd[v] || rev[v + 1] < rev[v]) return false;
            }

            // (C) Degree consistency — count input edges per src / dst.
            std::array<uint32_t, kMaxOps> src_count{};
            std::array<uint32_t, kMaxOps> dst_count{};
            for (uint32_t e = 0; e < num_edges; ++e) {
                ++src_count[spec.edges[e].src];
                ++dst_count[spec.edges[e].dst];
            }
            for (uint32_t v = 0; v < num_ops; ++v) {
                if (fwd[v + 1] - fwd[v] != src_count[v]) return false;
                if (rev[v + 1] - rev[v] != dst_count[v]) return false;
            }

            // (B) Bucketing + stability + preservation via an INDEPENDENT
            //     stable sort over edge indices (vs build_csr's counting
            //     sort).  Forward = stable-by-src, reverse = stable-by-dst.
            if (num_edges > 0) {
                std::array<uint32_t, kMaxEdges> order{};
                for (uint32_t e = 0; e < num_edges; ++e) order[e] = e;

                // Forward: stable sort indices by src.
                std::stable_sort(order.begin(), order.begin() + num_edges,
                    [&](uint32_t x, uint32_t y) {
                        return spec.edges[x].src < spec.edges[y].src;
                    });
                for (uint32_t k = 0; k < num_edges; ++k) {
                    if (!edge_eq(graph.fwd_edges[k], in_edges[order[k]])) return false;
                }

                // Reverse: stable sort indices by dst.
                for (uint32_t e = 0; e < num_edges; ++e) order[e] = e;
                std::stable_sort(order.begin(), order.begin() + num_edges,
                    [&](uint32_t x, uint32_t y) {
                        return spec.edges[x].dst < spec.edges[y].dst;
                    });
                for (uint32_t k = 0; k < num_edges; ++k) {
                    if (!edge_eq(graph.rev_edges[k], in_edges[order[k]])) return false;
                }
            }

            // (D) Determinism — rebuild on a fresh arena, compare bytes.
            Edge in_edges2[kMaxEdges]{};
            materialize(spec, in_edges2);
            Arena arena2{1 << 16};
            TraceGraph graph2{};
            build_csr(test.alloc, arena2, &graph2, in_edges2, num_edges, num_ops);
            for (uint32_t v = 0; v <= num_ops; ++v) {
                if (graph2.fwd_offsets[v] != fwd[v]) return false;
                if (graph2.rev_offsets[v] != rev[v]) return false;
            }
            for (uint32_t k = 0; k < num_edges; ++k) {
                if (!edge_eq(graph2.fwd_edges[k], graph.fwd_edges[k])) return false;
                if (!edge_eq(graph2.rev_edges[k], graph.rev_edges[k])) return false;
            }

            return true;
        });
}
