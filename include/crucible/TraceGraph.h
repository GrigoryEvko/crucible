#pragma once

#include <cstdint>

#include <crucible/MerkleDag.h>
#include <fixy/Core.h>
#include <fixy/Mutation.h>
#include <fixy/Refined.h>
#include <foundation/contracts/Decide.h>
#include <foundation/contracts/Post.h>
#include <foundation/contracts/Pre.h>
#include <foundation/effects/Effect.h>

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
    void set_src_port(uint8_t p) noexcept {
        CRUCIBLE_PRE(::foundation::decide::in_range<uint8_t>(p, 0, kMaxPort));
        src_port = p;
        CRUCIBLE_POST(0, src_port == p);
    }
    void set_dst_port(uint8_t p) noexcept {
        CRUCIBLE_PRE(::foundation::decide::in_range<uint8_t>(p, 0, kMaxPort));
        dst_port = p;
        CRUCIBLE_POST(0, dst_port == p);
    }
};

// The owner of the epoch arena of the build of a trace.  The background
// thread holds one for its build epoch, and each test or bench that builds a
// trace holds its own.  Two objects of the class share the brand of the
// arena, which no type tells apart.
class TraceScratch {
public:
    struct[[= ::foundation::brand::arena_member_brand{}]] Arena final {};
    using Epoch = ::foundation::core::Epoch<::fixy::EpochOf<Arena>>;
    explicit TraceScratch(::fixy::ArenaBlockSize block_size) noexcept
        : arena{::fixy::mint_epoch_arena<Arena>(block_size)} {}
    ::fixy::EpochArena<Arena> arena;
};

// A graph lives in an epoch of a TraceScratch, and its edges are runs of that
// epoch.  The class states the brand of the epoch, so it is a carrier of the
// brand: the rule brand-escape refuses a graph in a static object and in a
// member of a class that does not state the brand, and an epoch refuses a
// graph as the result of its body.  The ops and the slots are runs of the DAG
// arena of the region of the build, which outlives the epoch.
struct TraceGraph {
    using brand_type = ::fixy::EpochOf<TraceScratch::Arena>;
    using BuiltCount = ::fixy::WriteOnce<uint32_t>;

    // The ops in trace order.
    ::fixy::ArenaRun<TraceEntry, DagArena> ops{};
    BuiltCount num_ops = ::fixy::mint_write_once<uint32_t>();

    // Sorted by source, so a walk answers which ops consume a given output.
    ::fixy::ArenaRun<Edge, brand_type> fwd_edges{};
    ::fixy::ArenaRun<uint32_t, brand_type> fwd_offsets{};  // num_ops + 1 entries

    // Sorted by destination, so a walk answers which op produced a given input.
    ::fixy::ArenaRun<Edge, brand_type> rev_edges{};
    ::fixy::ArenaRun<uint32_t, brand_type> rev_offsets{};  // num_ops + 1 entries

    BuiltCount num_edges = ::fixy::mint_write_once<uint32_t>();

    ::fixy::ArenaRun<TensorSlot, DagArena> slots{};
    BuiltCount num_slots = ::fixy::mint_write_once<uint32_t>();  // count of distinct storages, not of tensors

    ContentHash content_hash;

    // The end of the run of the metadata log that this trace read, as a count
    // of records (MetaLog::head).  The ops hold a copy of that run in the
    // arena.  The owner of the log can release the run after the build.
    ::fixy::WriteOnce<uint64_t> max_meta_end = ::fixy::mint_write_once<uint64_t>();

    // The edges whose source is op i.  The arrays are in the arena, and not
    // in this struct.  The four bodies are in src/TraceGraph.cpp, because no
    // hot path reads the edges.
    [[nodiscard]] ::fixy::View<const Edge> fwd_of(OpIndex i) const noexcept;
    [[nodiscard]] uint32_t out_degree(OpIndex i) const noexcept;

    // The edges whose destination is op i.
    [[nodiscard]] ::fixy::View<const Edge> rev_of(OpIndex i) const noexcept;
    [[nodiscard]] uint32_t in_degree(OpIndex i) const noexcept;

    [[nodiscard, gnu::pure]] const TraceEntry& op(OpIndex i) const noexcept {
        return *ops.view().get(checked_index_(i)).expect("the build gives the graph one entry for each op");
    }

    // The precondition refuses a graph with no ops, where the fold
    // legitimately produces a zero hash. A caller that tolerates that
    // sentinel branches on the op count first and reads the field directly.
    [[nodiscard]] ValidContentHash computed_content_hash() const noexcept {
        CRUCIBLE_PRE(::foundation::decide::is_non_zero(content_hash));
        return ::fixy::mint_refined<::fixy::non_zero>(content_hash);
    }

private:
    // The one index check every accessor above runs.  The zero-count guard
    // is not redundant with the range check: with no ops the subtraction
    // wraps to the largest uint32_t and the range check then admits every
    // index.
    [[nodiscard, gnu::pure]] uint32_t checked_index_(OpIndex i) const noexcept {
        const uint32_t n_ops = num_ops.get();
        CRUCIBLE_PRE(n_ops > 0u);
        CRUCIBLE_PRE(::foundation::decide::in_range<std::uint32_t>(i.raw(), 0u, n_ops - 1u));
        return i.raw();
    }

    // The offset at index of a CSR offset array.  The build gives each array
    // num_ops + 1 entries, so each index below that has an offset.
    [[nodiscard]] static uint32_t offset_(::fixy::View<const uint32_t> offsets, uint32_t index) noexcept;

    // The edges between the offsets of op index and of the op after it.
    [[nodiscard]] static ::fixy::View<const Edge>
    edges_of_(::fixy::View<const Edge> edges, ::fixy::View<const uint32_t> offsets, uint32_t index) noexcept;
};

// A Ref of a graph, with the brand of the epoch that holds it.
using TraceGraphRef = ::fixy::Ref<TraceGraph, TraceGraph::brand_type>;

// The graph is in the epoch, and the build stage keeps its Ref for the length
// of the epoch.  The bodies of this function and of build_csr are in
// src/TraceGraph.cpp, so a unit that includes this header compiles no part of
// the build.
[[nodiscard]] TraceGraphRef alloc_trace_graph(::foundation::effects::Alloc a,
                                              TraceScratch::Epoch& epoch CRUCIBLE_LIFETIMEBOUND) noexcept;

// The graph is in the epoch, and an epoch lives only in the frame of
// with_epoch.  This twin refuses a temporary epoch.
void alloc_trace_graph(::foundation::effects::Alloc, TraceScratch::Epoch&&) =
    delete("a temporary epoch releases the graph at the end of the statement. Use the epoch that with_epoch gives");

namespace detail {

// The element at index of an array of the CSR build.  The counting sort
// computes each index below the count of the array.
template <class T>
[[nodiscard]] T& csr_element_(::fixy::View<T> elements, uint32_t index) noexcept {
    return *elements.get(index).expect("a CSR index is below the count of its array");
}

}  // namespace detail

// Counting sort into both adjacency arrays, linear in ops plus edges. The
// caller owns the graph struct. Only the arrays inside it are allocated here.
// The source and the destination of each edge are below num_ops.
void build_csr(::foundation::effects::Alloc a, TraceScratch::Epoch& epoch, TraceGraph& graph,
               ::fixy::View<const Edge> edges, uint32_t num_ops);

}  // namespace crucible
