#pragma once

// ── crucible::concurrent::Pipeline<Stages...> ────────────────────────
//
// Tier 3 commit 2 — composes N Stage<auto FnPtr_i, Ctx_i>'s into a
// chain where the output payload type of stage_i equals the input
// payload type of stage_{i+1}.  Built on Stage (Tier 3 commit 1,
// concurrent/Stage.h); together they ship the integration substrate's
// Tier 3 row per CLAUDE.md §XXI.
//
// ── What this header ships ──────────────────────────────────────────
//
//   IsStage<T>                      — recognizer for Stage<FnPtr, Ctx>
//                                     specializations.
//
//   stages_chain<S1, S2>            — pairwise compatibility: output
//                                     value of S1 == input value of S2
//                                     (after cv-ref strip, via
//                                     Stage::input_value_type /
//                                     Stage::output_value_type).
//
//   pipeline_chain<Stages...>       — N-ary fold of stages_chain over
//                                     adjacent pairs.  Vacuously true
//                                     for N ≤ 1.
//
//   CtxFitsPipeline<Ctx, Stages...> — single concept gate for
//                                     mint_pipeline.  Conjunction of
//                                     IsExecCtx<Ctx>,
//                                     (IsStage<Stages> && ...), and
//                                     pipeline_chain<Stages...>, and
//                                     pipeline_row_union_t<Stages...>
//                                     admitted by Ctx::row_type.
//
//   aggregate_per_call_ws_v<...>    — sum of each stage's handle-level
//                                     per-call working set.
//
//   Pipeline<Stages...>             — value-typed bundle of N stages,
//                                     held in a std::tuple.
//                                     Move-only.  &&-qualified .run()
//                                     uses a cache-tier dispatch rule:
//                                     inline only for explicitly
//                                     inline-safe stages with aggregate
//                                     WS ≤ private L2; otherwise one
//                                     std::jthread per stage.
//
//   mint_pipeline<>(ctx, stages...) — Universal Mint Pattern factory;
//                                     ctx-bound flavor; single-concept
//                                     gate; [[nodiscard]] noexcept;
//                                     consumes each stage by move.
//
//   StageGraph<StagePack<...>, EdgePack<...>>
//                                  — GAPS-086 DAG surface.  Edges are
//                                    StageEdge<From, To, FromOutput,
//                                    ToInput> over StagePack index
//                                    positions.  StagePack order is
//                                    the topological order; a back-edge
//                                    is rejected as a cycle witness.
//
//   PipelineDag<Graph> / mint_pipeline_dag(ctx, graph, stages...)
//                                  — non-linear fan-in/fan-out
//                                    composition.  Runtime still runs
//                                    one worker per stage unless every
//                                    node explicitly opts into the
//                                    finite inline path and the
//                                    aggregate working set fits the
//                                    private-cache gate.
//
// ── Why .run() usually spawns threads ────────────────────────────────
//
// Each Stage's body (the FnPtr the user wrote) IS the drain loop —
// it spins on try_pop until upstream closes its channel, processes
// each message, and writes downstream via try_push.  Sequential
// invocation of N stages would deadlock: stage_0's drain loop blocks
// waiting for stage_1 to consume from the channel between them, but
// stage_1 hasn't started yet.
//
// Pipeline.run() therefore keeps the thread-per-stage path as the
// default.  A stage may opt into inline execution by specializing
// stage_inline_safe<Stage>; this is for bounded micro-stages whose
// body is a single finite call, NOT a channel drain loop.  Opt-in is
// ignored unless both handles expose static per_call_working_set facts
// and the aggregate working set fits in the probed private L2.
// PipelineDag follows the same execution rule; its graph edges are a
// compile-time compatibility/placement contract, not a runtime scheduler
// queue.  Stages communicate through their already-minted endpoint
// handles.
//
// The spawned path joins via the std::array<jthread, N> destructor at
// fn epilogue.  This is identical in shape to permission_fork
// (safety/PermissionFork.h); the only difference is what's being
// forked: there it's permissions over disjoint regions, here it's
// already-bundled stages over already-connected channels.
//
// Cost:
//   inline path: N direct FnPtr calls, no thread creation.
//   spawned path: N pthread_create + N pthread_join per .run() call;
//                 each worker is best-effort pinned to one probed
//                 L3/cache cluster to keep the pipeline NUMA-local.
// For long-lived pipelines (the typical Crucible shape — a Vigil's
// background drain pipeline, a kernel-compile pool's stage chain) the
// thread spawn cost amortizes to zero.
//
// ── Stage-level fit checks happen at Stage mint ─────────────────────
//
// Each Stage was minted via mint_stage<FnPtr_i>(ctx_i, in_i, out_i),
// at which point CtxFitsStage<FnPtr_i, Ctx_i> validated the per-stage
// invariants.  The endpoint handles in_i and out_i themselves came
// from Endpoint mints (Tier 2) that already validated
// SubstrateFitsCtxResidency.  Pipeline's remaining jobs are:
//   * verify the CHAIN invariant (output_i ≡ input_{i+1}); and
//   * verify coordinator row admission.  The union of all stage rows,
//     pipeline_row_union_t<Stages...>, must be admitted by the
//     coordinator Ctx::row_type.
//
// The row-union gate is pinned by
// test/effects_neg/neg_mint_pipeline_{row_union_exceeds_ctx,
// chain_payload_compat_ok_row_mismatch,capability_propagation}.cpp and
// emits safety::diag::EffectRowMismatch at the mint boundary.
//
// ── Universal Mint Pattern compliance ───────────────────────────────
//
//   * Name: mint_pipeline (mint_<noun>, §XXI rule).
//   * First parameter: Ctx const& (ctx-bound mint flavor, §XXI).
//   * Single authorization boundary: ctx + pipeline_chain in the
//     requires clause; coordinator row admission via EffectRowMismatch
//     static assertion before constructing the Pipeline.
//   * [[nodiscard]] constexpr noexcept (no allocation in mint itself;
//     the jthread allocations happen at .run() time).
//   * Returns concrete Pipeline<Stages...> — never type-erased.
//   * Discoverable via `grep "mint_pipeline"`.
//   * HS14 negative-compile fixtures alongside.
//
// ── Axiom coverage ──────────────────────────────────────────────────
//
//   TypeSafe — chain-compatibility checked structurally; mismatched
//              payload types fail at the requires-clause boundary.
//   InitSafe — pure type-level construction; tuple of moved-in stages.
//   MemSafe  — Pipeline owns the N stages by value; jthread array on
//              the stack joins via dtor; no heap allocation by
//              Pipeline itself (jthreads are themselves heap-backed
//              but managed by std::jthread).
//   BorrowSafe — Pipeline is move-only; each Stage in the tuple is
//              also move-only; copies would duplicate the linear
//              Permission tokens carried by Stage's handles.
//   ThreadSafe — channels between stages are responsible for
//              cross-thread ordering (PermissionedSpscChannel et al.);
//              Pipeline only orchestrates the spawn/join.
//   LeakSafe — RAII jthread join at .run() epilogue guarantees no
//              orphan threads; std::array destructor joins in reverse
//              order (immaterial to correctness — bodies must all
//              complete before .run() returns).
//   DetSafe  — same (FnPtr_i, Ctx_i, handle pairings) → same
//              Pipeline type and same body invocations.
//
// Runtime cost: sizeof(Pipeline<S1, S2, ...>) ≈ sum of sizeof(S_i).
// Per .run() call: N pthread_create + N pthread_join (Linux ~5-15 μs
// each).  Right primitive for "N stages, long bodies" — NOT for
// "thousands of micro-stages" (use ChaseLevDeque + ThreadPool then).
//
// ── References ──────────────────────────────────────────────────────
//
//   concurrent/Stage.h             — Tier 3 commit 1 (the unit being
//                                    composed)
//   safety/PipelineStage.h         — FOUND-D19 (the FnPtr shape)
//   safety/PermissionFork.h        — analogous RAII fork-join pattern
//   CLAUDE.md §XXI                 — Universal Mint Pattern
//   CLAUDE.md §IX                  — concurrency cost ordering
//   CLAUDE.md §XVIII HS14          — neg-compile fixture requirement

#include <crucible/Platform.h>
#include <crucible/concurrent/PermissionedSpscChannel.h>
#include <crucible/concurrent/Stage.h>
#include <crucible/concurrent/Topology.h>
#include <crucible/concurrent/WorkingSet.h>
#include <crucible/effects/ExecCtx.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/permissions/Permission.h>
#include <crucible/safety/Decide.h>
#include <crucible/safety/diag/RowMismatch.h>

#include <array>
#include <cstdint>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>

#if __has_include(<pthread.h>) && __has_include(<sched.h>)
  #include <pthread.h>
  #include <sched.h>
  #define CRUCIBLE_PIPELINE_HAS_PTHREAD_AFFINITY 1
#else
  #define CRUCIBLE_PIPELINE_HAS_PTHREAD_AFFINITY 0
#endif

namespace crucible::concurrent {

// ═════════════════════════════════════════════════════════════════════
// ── IsStage<T> ─────────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Class-template specialization recognizer for Stage<FnPtr, Ctx>.
// Detail-namespaced trait + user-facing concept.

namespace detail {

template <class T>
struct is_stage : std::false_type {};

template <auto FnPtr, class Ctx>
struct is_stage<Stage<FnPtr, Ctx>> : std::true_type {};

template <auto FnPtr, class Ctx, class Inputs, class Outputs>
struct is_stage<MpmcStage<FnPtr, Ctx, Inputs, Outputs>> : std::true_type {};

template <auto FnPtr, class Ctx>
struct is_stage<SwmrStage<FnPtr, Ctx>> : std::true_type {};

template <class Stage>
struct stage_ports;

template <auto FnPtr, class Ctx>
struct stage_ports<Stage<FnPtr, Ctx>> {
    static constexpr std::size_t input_count = 1;
    static constexpr std::size_t output_count = 1;

    template <std::size_t I>
        requires (I == 0)
    using input_value_type = typename Stage<FnPtr, Ctx>::input_value_type;

    template <std::size_t I>
        requires (I == 0)
    using output_value_type = typename Stage<FnPtr, Ctx>::output_value_type;
};

template <auto FnPtr, class Ctx, class Inputs, class Outputs>
struct stage_ports<MpmcStage<FnPtr, Ctx, Inputs, Outputs>> {
    using stage_type = MpmcStage<FnPtr, Ctx, Inputs, Outputs>;
    static constexpr std::size_t input_count = stage_type::input_count;
    static constexpr std::size_t output_count = stage_type::output_count;

    template <std::size_t I>
        requires (I < input_count)
    using input_value_type = typename stage_type::template input_value_type<I>;

    template <std::size_t I>
        requires (I < output_count)
    using output_value_type = typename stage_type::template output_value_type<I>;
};

template <auto FnPtr, class Ctx>
struct stage_ports<SwmrStage<FnPtr, Ctx>> {
    using stage_type = SwmrStage<FnPtr, Ctx>;
    static constexpr std::size_t input_count = 1;
    static constexpr std::size_t output_count = 1;

    template <std::size_t I>
        requires (I == 0)
    using input_value_type = typename stage_type::input_value_type;

    template <std::size_t I>
        requires (I == 0)
    using output_value_type = typename stage_type::output_value_type;
};

}  // namespace detail

template <class T>
concept IsStage = detail::is_stage<std::remove_cvref_t<T>>::value;

template <class Stage>
    requires IsStage<Stage>
inline constexpr std::size_t stage_input_count_v =
    detail::stage_ports<std::remove_cvref_t<Stage>>::input_count;

template <class Stage>
    requires IsStage<Stage>
inline constexpr std::size_t stage_output_count_v =
    detail::stage_ports<std::remove_cvref_t<Stage>>::output_count;

template <class Stage, std::size_t I>
    requires IsStage<Stage>
using stage_input_value_t =
    typename detail::stage_ports<std::remove_cvref_t<Stage>>
        ::template input_value_type<I>;

template <class Stage, std::size_t I>
    requires IsStage<Stage>
using stage_output_value_t =
    typename detail::stage_ports<std::remove_cvref_t<Stage>>
        ::template output_value_type<I>;

// ═════════════════════════════════════════════════════════════════════
// ── stages_chain<S1, S2> ───────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Pairwise compatibility: stage S1's output payload type equals
// stage S2's input payload type.  Both must be IsStage; the equality
// uses Stage's exposed input_value_type / output_value_type aliases
// (cv-ref-stripped at extraction time per FOUND-D19's
// pipeline_stage_input_value_t / pipeline_stage_output_value_t).

template <class S1, class S2>
concept stages_chain =
    IsStage<S1>
 && IsStage<S2>
 && stage_output_count_v<S1> == 1
 && stage_input_count_v<S2> == 1
 && std::is_same_v<
        stage_output_value_t<S1, 0>,
        stage_input_value_t<S2, 0>>;

// ═════════════════════════════════════════════════════════════════════
// ── pipeline_chain<Stages...> ──────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// N-ary fold over adjacent stage pairs.  Empty pack and single-stage
// pack are vacuously chain-compatible (no adjacent pairs to check).
// For N ≥ 2: every adjacent (i, i+1) must satisfy stages_chain.

namespace detail {

template <class Tuple, std::size_t... Is>
consteval bool pipeline_chain_check(std::index_sequence<Is...>) noexcept {
    if constexpr (sizeof...(Is) == 0) {
        return true;  // ≤ 1 stage — vacuously chained
    } else {
        return ((stages_chain<
                    std::tuple_element_t<Is,     Tuple>,
                    std::tuple_element_t<Is + 1, Tuple>>) && ...);
    }
}

}  // namespace detail

template <class... Stages>
concept pipeline_chain =
    sizeof...(Stages) >= 1
 && (IsStage<Stages> && ...)
 && detail::pipeline_chain_check<std::tuple<std::remove_cvref_t<Stages>...>>(
        std::make_index_sequence<(sizeof...(Stages) > 0 ? sizeof...(Stages) - 1 : 0)>{});

// ═════════════════════════════════════════════════════════════════════
// ── CtxFitsPipeline<Ctx, Stages...> ────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Soundness gate for mint_pipeline.  Conjunction of:
//
//   1. IsExecCtx<Ctx> — Ctx is a well-formed ExecCtx with the four
//      static facts.  (Ctx is the COORDINATOR ctx for Pipeline, not
//      a per-stage ctx — those live inside each Stage.)
//
//   2. pipeline_chain<Stages...> — IsStage on each, plus adjacent-pair
//      payload-type compatibility folded across the pack.
//
//   3. pipeline_row_union_t<Stages...> ⊆ Ctx::row_type — the
//      coordinator must admit every effect row represented by the
//      staged execution contexts it is about to run.
//
// Per-stage CtxFitsStage was checked at each Stage's mint boundary;
// re-checking payload rows here would be redundant.

namespace detail {

inline void pipeline_row_admission_anchor_() noexcept {}

template <class... Stages>
struct pipeline_row_union_impl;

template <>
struct pipeline_row_union_impl<> {
    using type = ::crucible::effects::Row<>;
};

template <class Stage0, class... Rest>
struct pipeline_row_union_impl<Stage0, Rest...> {
    using stage_row = typename std::remove_cvref_t<Stage0>::ctx_type::row_type;
    using rest_row = typename pipeline_row_union_impl<Rest...>::type;
    using type = ::crucible::effects::row_union_t<stage_row, rest_row>;
};

}  // namespace detail

template <class... Stages>
using pipeline_row_union_t =
    typename detail::pipeline_row_union_impl<std::remove_cvref_t<Stages>...>::type;

template <class Ctx, class... Stages>
concept CtxFitsPipeline =
    ::crucible::effects::IsExecCtx<Ctx>
 && pipeline_chain<Stages...>
 && ::crucible::decide::row_subset<pipeline_row_union_t<Stages...>,
                                   typename Ctx::row_type>();

// ═════════════════════════════════════════════════════════════════════
// ── StageGraph / PipelineDag — GAPS-086 non-linear composition ─────
// ═════════════════════════════════════════════════════════════════════

template <class... Stages>
struct StagePack {};

template <class... Edges>
struct EdgePack {};

template <std::size_t From,
          std::size_t To,
          std::size_t FromOutput = 0,
          std::size_t ToInput = 0>
struct StageEdge {
    static constexpr std::size_t from = From;
    static constexpr std::size_t to = To;
    static constexpr std::size_t from_output = FromOutput;
    static constexpr std::size_t to_input = ToInput;
};

template <class Stages, class Edges>
struct StageGraph {};

namespace detail {

template <class T>
struct is_stage_edge : std::false_type {};

template <std::size_t From,
          std::size_t To,
          std::size_t FromOutput,
          std::size_t ToInput>
struct is_stage_edge<StageEdge<From, To, FromOutput, ToInput>>
    : std::true_type {};

template <class T>
struct is_stage_graph : std::false_type {};

template <class... Stages, class... Edges>
struct is_stage_graph<StageGraph<StagePack<Stages...>, EdgePack<Edges...>>>
    : std::true_type {};

template <class Graph>
struct stage_graph_traits;

template <class... Stages, class... Edges>
struct stage_graph_traits<
    StageGraph<StagePack<Stages...>, EdgePack<Edges...>>> {
    using stage_pack_type = StagePack<Stages...>;
    using edge_pack_type = EdgePack<Edges...>;
    using stage_tuple = std::tuple<Stages...>;
    using edge_tuple = std::tuple<Edges...>;
    static constexpr std::size_t stage_count = sizeof...(Stages);
    static constexpr std::size_t edge_count = sizeof...(Edges);
};

template <class Graph, class Edge>
consteval bool stage_graph_edge_valid() noexcept {
    using traits = stage_graph_traits<Graph>;
    constexpr std::size_t n = traits::stage_count;
    if constexpr (!is_stage_edge<Edge>::value) {
        return false;
    } else if constexpr (Edge::from >= n || Edge::to >= n) {
        return false;
    } else if constexpr (Edge::from >= Edge::to) {
        return false;
    } else {
        using from_stage =
            std::tuple_element_t<Edge::from, typename traits::stage_tuple>;
        using to_stage =
            std::tuple_element_t<Edge::to, typename traits::stage_tuple>;

        if constexpr (Edge::from_output >= stage_output_count_v<from_stage>
                   || Edge::to_input >= stage_input_count_v<to_stage>) {
            return false;
        } else {
            return std::is_same_v<
                stage_output_value_t<from_stage, Edge::from_output>,
                stage_input_value_t<to_stage, Edge::to_input>>;
        }
    }
}

template <class Graph, class... Edges>
consteval bool stage_graph_connected_impl(EdgePack<Edges...>) noexcept {
    using traits = stage_graph_traits<Graph>;
    constexpr std::size_t n = traits::stage_count;
    if constexpr (n <= 1) {
        return true;
    } else {
        std::array<bool, n> reached{};
        reached[0] = true;

        bool changed = true;
        while (changed) {
            changed = false;
            auto propagate = [&]<class Edge>() consteval {
                if (reached[Edge::from] && !reached[Edge::to]) {
                    reached[Edge::to] = true;
                    changed = true;
                }
                if (reached[Edge::to] && !reached[Edge::from]) {
                    reached[Edge::from] = true;
                    changed = true;
                }
            };
            (propagate.template operator()<Edges>(), ...);
        }

        for (bool seen : reached) {
            if (!seen) return false;
        }
        return true;
    }
}

template <class Graph>
consteval bool stage_graph_connected() noexcept {
    using traits = stage_graph_traits<Graph>;
    return stage_graph_connected_impl<Graph>(
        typename traits::edge_pack_type{});
}

template <class Graph, std::size_t... Is>
consteval bool stage_graph_all_stages(
    std::index_sequence<Is...>) noexcept {
    using traits = stage_graph_traits<Graph>;
    return ((IsStage<std::tuple_element_t<Is, typename traits::stage_tuple>>) && ...);
}

template <class Graph, class... Edges>
consteval bool stage_graph_all_edges_valid(EdgePack<Edges...>) noexcept {
    return (stage_graph_edge_valid<Graph, Edges>() && ...);
}

template <class Graph>
consteval bool stage_graph_well_formed() noexcept {
    if constexpr (!is_stage_graph<Graph>::value) {
        return false;
    } else {
        using traits = stage_graph_traits<Graph>;
        if constexpr (traits::stage_count == 0) {
            return false;
        } else if constexpr (!stage_graph_all_stages<Graph>(
                                 std::make_index_sequence<
                                     traits::stage_count>{})) {
            return false;
        } else if constexpr (!stage_graph_all_edges_valid<Graph>(
                                 typename traits::edge_pack_type{})) {
            return false;
        } else {
            return stage_graph_connected<Graph>();
        }
    }
}

template <class Graph>
struct stage_graph_row_union;

template <class... Stages, class... Edges>
struct stage_graph_row_union<
    StageGraph<StagePack<Stages...>, EdgePack<Edges...>>> {
    using type = pipeline_row_union_t<Stages...>;
};

inline void pipeline_dag_row_admission_anchor_() noexcept {}

}  // namespace detail

template <class T>
concept IsStageEdge = detail::is_stage_edge<std::remove_cvref_t<T>>::value;

template <class T>
concept IsStageGraph =
    detail::is_stage_graph<std::remove_cvref_t<T>>::value;

template <class Graph>
concept StageGraphWellFormed =
    IsStageGraph<Graph>
 && detail::stage_graph_well_formed<std::remove_cvref_t<Graph>>();

template <class Graph>
    requires StageGraphWellFormed<Graph>
using stage_graph_row_union_t =
    typename detail::stage_graph_row_union<std::remove_cvref_t<Graph>>::type;

template <class Ctx, class Graph>
concept CtxFitsPipelineDag =
    ::crucible::effects::IsExecCtx<Ctx>
 && StageGraphWellFormed<Graph>
 && ::crucible::decide::row_subset<stage_graph_row_union_t<Graph>,
                                   typename Ctx::row_type>();

// ═════════════════════════════════════════════════════════════════════
// ── Pipeline working-set and inline-safety traits ──────────────────
// ═════════════════════════════════════════════════════════════════════

template <class Stage>
struct stage_inline_safe : std::false_type {};

template <class Stage>
inline constexpr bool stage_inline_safe_v =
    stage_inline_safe<std::remove_cvref_t<Stage>>::value;

namespace detail {

template <class Stage, bool = IsStage<Stage>>
struct stage_working_set_traits {
    static constexpr bool known = false;
    static constexpr std::size_t value = unknown_per_call_working_set;
};

template <class Stage>
struct stage_working_set_traits<Stage, true> {
private:
    using S = std::remove_cvref_t<Stage>;

public:
    static constexpr bool known = [] consteval {
        if constexpr (requires { S::aggregate_working_set_known; }) {
            return S::aggregate_working_set_known;
        } else {
            using In = typename S::consumer_handle_type;
            using Out = typename S::producer_handle_type;
            return has_static_per_call_working_set_v<In>
                && has_static_per_call_working_set_v<Out>;
        }
    }();

    static constexpr std::size_t value = [] consteval {
        if constexpr (requires { S::aggregate_per_call_working_set; }) {
            return S::aggregate_per_call_working_set;
        } else if constexpr (known) {
            using In = typename S::consumer_handle_type;
            using Out = typename S::producer_handle_type;
            return saturating_ws_add(
                per_call_working_set_of_v<In>,
                per_call_working_set_of_v<Out>);
        } else {
            return unknown_per_call_working_set;
        }
    }();
};

template <class... Stages>
[[nodiscard]] consteval std::size_t aggregate_stage_ws() noexcept {
    std::size_t total = 0;
    ((total = saturating_ws_add(
          total,
          stage_working_set_traits<std::remove_cvref_t<Stages>>::value)), ...);
    return total;
}

}  // namespace detail

template <class Stage>
inline constexpr bool stage_per_call_ws_known_v =
    detail::stage_working_set_traits<std::remove_cvref_t<Stage>>::known;

template <class Stage>
inline constexpr std::size_t stage_per_call_ws_v =
    detail::stage_working_set_traits<std::remove_cvref_t<Stage>>::value;

template <class... Stages>
inline constexpr bool aggregate_per_call_ws_known_v =
    (stage_per_call_ws_known_v<Stages> && ...);

template <class... Stages>
inline constexpr std::size_t aggregate_per_call_ws_v =
    detail::aggregate_stage_ws<std::remove_cvref_t<Stages>...>();

template <class... Stages>
inline constexpr bool pipeline_inline_safe_v =
    (stage_inline_safe_v<Stages> && ...);

enum class PipelineDispatchKind : std::uint8_t {
    Inline,
    ThreadPerStage,
};

namespace detail {

[[nodiscard]] inline int
pipeline_affinity_cpu_(const Topology& topology,
                       std::size_t worker_index) noexcept {
    auto clusters = topology.cache_clusters();
    if (!clusters.empty()) {
        const std::vector<int>* best = &clusters.front();
        for (auto const& cluster : clusters) {
            if (cluster.size() > best->size()) best = &cluster;
        }
        if (!best->empty()) {
            return (*best)[worker_index % best->size()];
        }
    }

    auto node0 = topology.cores_on_node(0);
    if (!node0.empty()) {
        return node0[worker_index % node0.size()];
    }

    return -1;
}

template <std::size_t N>
[[nodiscard]] inline const std::array<int, N>&
pipeline_affinity_cpus_() noexcept {
    static const std::array<int, N> cpus = [] {
        std::array<int, N> out{};
        out.fill(-1);

        const auto& topology = Topology::instance();
        for (std::size_t i = 0; i < N; ++i) {
            out[i] = pipeline_affinity_cpu_(topology, i);
        }
        return out;
    }();
    return cpus;
}

inline void pin_current_pipeline_thread_(int cpu) noexcept {
#if CRUCIBLE_PIPELINE_HAS_PTHREAD_AFFINITY
    if (cpu < 0) return;
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(static_cast<std::size_t>(cpu), &set);
    (void)::pthread_setaffinity_np(::pthread_self(), sizeof(set), &set);
#else
    (void)cpu;
#endif
}

}  // namespace detail

// ═════════════════════════════════════════════════════════════════════
// ── Pipeline<Stages...> ────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

template <class... Stages>
    requires pipeline_chain<Stages...>
class Pipeline {
public:
    static constexpr std::size_t arity = sizeof...(Stages);
    static constexpr std::size_t aggregate_per_call_working_set =
        aggregate_per_call_ws_v<Stages...>;
    static constexpr bool inline_safe =
        pipeline_inline_safe_v<Stages...>;
    static constexpr bool aggregate_working_set_known =
        aggregate_per_call_ws_known_v<Stages...>;

    static_assert(
        ((!stage_inline_safe_v<Stages> || stage_per_call_ws_known_v<Stages>) && ...),
        "Pipeline inline opt-in requires both stage handles to expose "
        "static constexpr per_call_working_set");

    // ── Move-only (held Stages are move-only) ──────────────────────
    Pipeline(Pipeline const&) = delete("Pipeline holds move-only Stages, each of which holds linear Permission tokens via its consumer/producer handles");
    Pipeline& operator=(Pipeline const&) = delete("Pipeline holds move-only Stages, each of which holds linear Permission tokens via its consumer/producer handles");
    Pipeline(Pipeline&&) noexcept = default;
    Pipeline& operator=(Pipeline&&) noexcept = default;

    // ── run() — cache-tier dispatch, consuming the pipeline ───────
    //
    // Inline path is only available for finite, explicitly opted-in
    // stages.  Default drain-loop stages stay on the jthread path.

    void run() && noexcept {
        if (will_run_inline()) {
            std::move(*this).run_inline_impl_(
                std::index_sequence_for<Stages...>{});
        } else {
            std::move(*this).run_threaded_impl_(
                std::index_sequence_for<Stages...>{});
        }
    }

    [[nodiscard]] static PipelineDispatchKind dispatch_kind() noexcept {
        static const PipelineDispatchKind kind = compute_dispatch_kind_();
        return kind;
    }

    [[nodiscard]] static bool will_run_inline() noexcept {
        return dispatch_kind() == PipelineDispatchKind::Inline;
    }

    // ── Accessor (introspection only — does not consume) ───────────
    template <std::size_t I>
        requires (I < sizeof...(Stages))
    [[nodiscard]] constexpr auto&       stage()       &  noexcept { return std::get<I>(stages_); }

    template <std::size_t I>
        requires (I < sizeof...(Stages))
    [[nodiscard]] constexpr auto const& stage() const &  noexcept { return std::get<I>(stages_); }

private:
    // ── Construction (used by mint_pipeline; not user-facing) ─────
    //
    // Private per CLAUDE.md §XXI — mint_pipeline is the single
    // load-bearing authorization point.  A direct call site like
    // `Pipeline<S1, S2>{s1, s2}` would bypass the
    // `CtxFitsPipeline<Ctx, Stages...>` admission check and emit a
    // pipeline whose effect row never sees `Subrow<required, ctx>`.
    [[nodiscard]] explicit constexpr Pipeline(Stages&&... stages) noexcept
        : stages_{std::forward<Stages>(stages)...}
    {}

    // mint_pipeline is the sole authorized constructor — friend the
    // entire template family so any (Ctx, Stages...) instantiation
    // can reach the private ctor.
    template <::crucible::effects::IsExecCtx MintCtx, class... MintStages>
        requires pipeline_chain<std::remove_cvref_t<MintStages>...>
    friend constexpr auto mint_pipeline(
        MintCtx const&,
        MintStages&&...) noexcept;

    [[nodiscard]] static PipelineDispatchKind compute_dispatch_kind_() noexcept {
        if constexpr (inline_safe && aggregate_working_set_known) {
            const auto& topology = Topology::instance();
            const std::size_t aggregate = aggregate_per_call_working_set;
            if (aggregate <= topology.l1d_per_core_bytes()) {
                return PipelineDispatchKind::Inline;
            }
            if (aggregate <= topology.l2_per_core_bytes()) {
                return PipelineDispatchKind::Inline;
            }
        }
        return PipelineDispatchKind::ThreadPerStage;
    }
    template <std::size_t... Is>
    void run_inline_impl_(std::index_sequence<Is...>) && noexcept {
        ((void)std::move(std::get<Is>(stages_)).run(), ...);
    }

    template <std::size_t... Is>
    void run_threaded_impl_(std::index_sequence<Is...>) && noexcept {
        const auto& affinity_cpus =
            detail::pipeline_affinity_cpus_<sizeof...(Is)>();

        // Build N jthreads in-place; each captures-by-move its stage
        // and invokes std::move(stage).run() in its body.  The array's
        // destructor (at this fn's epilogue) joins all threads.
        [[maybe_unused]] std::array<std::jthread, sizeof...(Is)> threads = {
            std::jthread{
                [stage = std::move(std::get<Is>(stages_)),
                 cpu = affinity_cpus[Is]]
                (std::stop_token) mutable noexcept {
                    detail::pin_current_pipeline_thread_(cpu);
                    std::move(stage).run();
                }
            }...
        };
        // ~std::array runs here, joining each jthread.
    }

    std::tuple<Stages...> stages_;
};

// ── pipeline_dag_mint_gate / CtxFitsPipelineDagMint ─────────────────
//
// Hoisted above the PipelineDag class body so the in-class friend
// declaration for mint_pipeline_dag can reference the same concept
// the free-function template definition uses below.  Without this
// hoist the friend declaration cannot see the concept and the
// signatures fail to match — the mint factory then cannot reach
// PipelineDag's private constructor.
namespace detail {

template <class Ctx, class Graph, class... Stages>
struct pipeline_dag_mint_gate {
private:
    static consteval bool compute() noexcept {
        if constexpr (!CtxFitsPipelineDag<Ctx, Graph>) {
            return false;
        } else {
            using expected =
                typename stage_graph_traits<std::remove_cvref_t<Graph>>
                    ::stage_pack_type;
            using actual = StagePack<std::remove_cvref_t<Stages>...>;
            return std::is_same_v<expected, actual>;
        }
    }

public:
    static constexpr bool value = compute();
};

}  // namespace detail

template <class Ctx, class Graph, class... Stages>
concept CtxFitsPipelineDagMint =
    detail::pipeline_dag_mint_gate<Ctx, Graph, Stages...>::value;

template <class Graph>
    requires StageGraphWellFormed<Graph>
class PipelineDag;

template <class... Stages, class... Edges>
    requires StageGraphWellFormed<
        StageGraph<StagePack<Stages...>, EdgePack<Edges...>>>
class PipelineDag<StageGraph<StagePack<Stages...>, EdgePack<Edges...>>> {
public:
    using graph_type = StageGraph<StagePack<Stages...>, EdgePack<Edges...>>;

    static constexpr std::size_t arity = sizeof...(Stages);
    static constexpr std::size_t edge_count = sizeof...(Edges);
    static constexpr std::size_t aggregate_per_call_working_set =
        aggregate_per_call_ws_v<Stages...>;
    static constexpr bool inline_safe =
        pipeline_inline_safe_v<Stages...>;
    static constexpr bool aggregate_working_set_known =
        aggregate_per_call_ws_known_v<Stages...>;

    static_assert(
        ((!stage_inline_safe_v<Stages> || stage_per_call_ws_known_v<Stages>) && ...),
        "PipelineDag inline opt-in requires every stage handle pack to "
        "expose static constexpr per_call_working_set");

    PipelineDag(PipelineDag const&) = delete(
        "PipelineDag holds move-only Stages, each of which owns endpoint handles");
    PipelineDag& operator=(PipelineDag const&) = delete(
        "PipelineDag holds move-only Stages, each of which owns endpoint handles");
    PipelineDag(PipelineDag&&) noexcept = default;
    PipelineDag& operator=(PipelineDag&&) noexcept = default;

    void run() && noexcept {
        if (will_run_inline()) {
            std::move(*this).run_inline_impl_(
                std::index_sequence_for<Stages...>{});
        } else {
            std::move(*this).run_threaded_impl_(
                std::index_sequence_for<Stages...>{});
        }
    }

    [[nodiscard]] static PipelineDispatchKind dispatch_kind() noexcept {
        static const PipelineDispatchKind kind = compute_dispatch_kind_();
        return kind;
    }

    [[nodiscard]] static bool will_run_inline() noexcept {
        return dispatch_kind() == PipelineDispatchKind::Inline;
    }

    template <std::size_t I>
        requires (I < sizeof...(Stages))
    [[nodiscard]] constexpr auto& stage() & noexcept {
        return std::get<I>(stages_);
    }

private:
    // ── Construction (used by mint_pipeline_dag; not user-facing) ──
    //
    // Private per CLAUDE.md §XXI — `mint_pipeline_dag` is the sole
    // authorization point.  A direct `PipelineDag<Graph>{stages...}`
    // call site would bypass `CtxFitsPipelineDagMint`'s row admission
    // (`Subrow<stage_graph_row_union_t<Graph>, ctx_row>`) and emit a
    // DAG whose effect row never sees the gate.
    [[nodiscard]] explicit constexpr PipelineDag(Stages&&... stages) noexcept
        : stages_{std::forward<Stages>(stages)...}
    {}

    // mint_pipeline_dag is the sole authorized constructor.  Friend the
    // entire template family so any (Ctx, Graph, Stages...) instantiation
    // matching the same CtxFitsPipelineDagMint constraint can reach the
    // private ctor.
    template <::crucible::effects::IsExecCtx MintCtx,
              class MintGraph,
              class... MintStages>
        requires CtxFitsPipelineDagMint<MintCtx, MintGraph, MintStages...>
    friend constexpr auto mint_pipeline_dag(
        MintCtx const&,
        MintGraph,
        MintStages&&...) noexcept;

    [[nodiscard]] static PipelineDispatchKind compute_dispatch_kind_() noexcept {
        if constexpr (inline_safe && aggregate_working_set_known) {
            const auto& topology = Topology::instance();
            const std::size_t aggregate = aggregate_per_call_working_set;
            if (aggregate <= topology.l1d_per_core_bytes()) {
                return PipelineDispatchKind::Inline;
            }
            if (aggregate <= topology.l2_per_core_bytes()) {
                return PipelineDispatchKind::Inline;
            }
        }
        return PipelineDispatchKind::ThreadPerStage;
    }

    template <std::size_t... Is>
    void run_inline_impl_(std::index_sequence<Is...>) && noexcept {
        ((void)std::move(std::get<Is>(stages_)).run(), ...);
    }

    template <std::size_t... Is>
    void run_threaded_impl_(std::index_sequence<Is...>) && noexcept {
        const auto& affinity_cpus =
            detail::pipeline_affinity_cpus_<sizeof...(Is)>();

        [[maybe_unused]] std::array<std::jthread, sizeof...(Is)> threads = {
            std::jthread{
                [stage = std::move(std::get<Is>(stages_)),
                 cpu = affinity_cpus[Is]]
                (std::stop_token) mutable noexcept {
                    detail::pin_current_pipeline_thread_(cpu);
                    std::move(stage).run();
                }
            }...
        };
    }

    std::tuple<Stages...> stages_;
};

// ═════════════════════════════════════════════════════════════════════
// ── mint_pipeline<>(ctx, stages...) — Universal Mint Pattern ───────
// ═════════════════════════════════════════════════════════════════════
//
// Ctx-bound mint factory per CLAUDE.md §XXI.  Each stage is consumed
// by move into the constructed Pipeline.  The single concept gate
// CtxFitsPipeline<Ctx, Stages...> validates IsExecCtx + IsStage-pack
// + chain-compatibility; failure produces a substitution diagnostic
// at the call site.

template <::crucible::effects::IsExecCtx Ctx, class... Stages>
    requires pipeline_chain<std::remove_cvref_t<Stages>...>
[[nodiscard]] constexpr auto mint_pipeline(
    Ctx const& /*ctx*/,
    Stages&&... stages) noexcept
{
    using ctx_row = typename Ctx::row_type;
    using required_row = pipeline_row_union_t<std::remove_cvref_t<Stages>...>;
    using offending_row =
        ::crucible::effects::row_difference_t<required_row, ctx_row>;

    CRUCIBLE_ROW_MISMATCH_ASSERT(
        (::crucible::decide::row_subset<required_row, ctx_row>()),
        EffectRowMismatch,
        &::crucible::concurrent::detail::pipeline_row_admission_anchor_,
        ctx_row,
        required_row,
        offending_row);

    return Pipeline<std::remove_cvref_t<Stages>...>{
        std::forward<Stages>(stages)...
    };
}

template <::crucible::effects::IsExecCtx Ctx, class Graph, class... Stages>
    requires CtxFitsPipelineDagMint<Ctx, Graph, Stages...>
[[nodiscard]] constexpr auto mint_pipeline_dag(
    Ctx const& /*ctx*/,
    Graph,
    Stages&&... stages) noexcept
{
    using ctx_row = typename Ctx::row_type;
    using required_row = stage_graph_row_union_t<Graph>;
    using offending_row =
        ::crucible::effects::row_difference_t<required_row, ctx_row>;

    CRUCIBLE_ROW_MISMATCH_ASSERT(
        (::crucible::decide::row_subset<required_row, ctx_row>()),
        EffectRowMismatch,
        &::crucible::concurrent::detail::pipeline_dag_row_admission_anchor_,
        ctx_row,
        required_row,
        offending_row);

    using graph_type = std::remove_cvref_t<Graph>;
    return PipelineDag<graph_type>{std::forward<Stages>(stages)...};
}

// ═════════════════════════════════════════════════════════════════════
// ── Self-test block ────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Pin admit/reject behavior across canonical Stage compositions:
// matched chains admit, mismatched chains reject; non-Stage elements
// reject; non-IsExecCtx ctx rejects.

namespace detail::pipeline_self_test {

namespace eff = ::crucible::effects;
namespace saf = ::crucible::safety::extract;

// Reuse Stage's self-test fixtures (FakeConsumer/FakeProducer +
// stage_pass_through / stage_transform_int_to_float).
using namespace ::crucible::concurrent::detail::stage_self_test;

// Additional fixture: a float-to-double transform stage to chain
// after the int-to-float one.
inline void stage_transform_float_to_double(FakeConsumer<float>&&,
                                            FakeProducer<double>&&) noexcept {}
static_assert(saf::PipelineStage<&stage_transform_float_to_double>);

// Fixture stages of distinct shapes for chain checks.
using S_int_to_int       = Stage<&stage_pass_through,            eff::HotFgCtx>;
using S_int_to_float     = Stage<&stage_transform_int_to_float,  eff::HotFgCtx>;
using S_float_to_double  = Stage<&stage_transform_float_to_double, eff::HotFgCtx>;
using S_bg_int_to_int    = Stage<&stage_pass_through,            eff::BgDrainCtx>;
using S_init_int_to_int  = Stage<&stage_pass_through,            eff::ColdInitCtx>;

// IsStage admits / rejects appropriately.
static_assert( IsStage<S_int_to_int>);
static_assert( IsStage<S_int_to_float>);
static_assert( IsStage<S_float_to_double>);
static_assert(!IsStage<int>);
static_assert(!IsStage<eff::HotFgCtx>);

// stages_chain admits matched pairs, rejects mismatched.
static_assert( stages_chain<S_int_to_int,      S_int_to_int>);       // int→int, int→int
static_assert( stages_chain<S_int_to_float,    S_float_to_double>);  // int→float, float→double
static_assert(!stages_chain<S_int_to_int,      S_float_to_double>);  // int→int, float→double (mismatch)
static_assert(!stages_chain<S_int_to_float,    S_int_to_int>);       // int→float, int→int (mismatch)
static_assert(!stages_chain<int,               S_int_to_int>);       // non-Stage
static_assert(!stages_chain<S_int_to_int,      int>);                // non-Stage

// pipeline_chain folds correctly.
static_assert( pipeline_chain<S_int_to_int>);                                              // N=1, vacuous
static_assert( pipeline_chain<S_int_to_int, S_int_to_int>);                                // N=2, matched
static_assert( pipeline_chain<S_int_to_int, S_int_to_float, S_float_to_double>);           // N=3, transforms align
static_assert( pipeline_chain<S_bg_int_to_int, S_int_to_int>);                             // context row is separate from payload chain
static_assert(!pipeline_chain<S_int_to_int, S_float_to_double>);                           // N=2, mismatch
static_assert(!pipeline_chain<S_int_to_int, S_int_to_int, S_float_to_double>);             // N=3, last pair mismatch
static_assert(!pipeline_chain<int>);                                                       // non-Stage
static_assert(!pipeline_chain<>);                                                          // empty pack

static_assert(eff::Subrow<pipeline_row_union_t<S_int_to_int>, eff::Row<>>);
static_assert(eff::Subrow<
    pipeline_row_union_t<S_bg_int_to_int>,
    eff::Row<eff::Effect::Bg, eff::Effect::Alloc>>);
static_assert(eff::Subrow<
    pipeline_row_union_t<S_bg_int_to_int, S_init_int_to_int>,
    eff::Row<eff::Effect::Bg, eff::Effect::Alloc,
             eff::Effect::Init, eff::Effect::IO>>);

// CtxFitsPipeline conjunction.
static_assert( CtxFitsPipeline<eff::HotFgCtx,   S_int_to_int>);
static_assert( CtxFitsPipeline<eff::BgDrainCtx, S_int_to_float, S_float_to_double>);
static_assert( CtxFitsPipeline<eff::BgDrainCtx, S_bg_int_to_int, S_int_to_int>);
static_assert(!CtxFitsPipeline<int,             S_int_to_int>);                            // non-Ctx
static_assert(!CtxFitsPipeline<eff::HotFgCtx,   S_int_to_int, S_float_to_double>);         // mismatch
static_assert(!CtxFitsPipeline<eff::HotFgCtx,   int>);                                     // non-Stage
static_assert(!CtxFitsPipeline<eff::HotFgCtx,   S_bg_int_to_int>);                         // coordinator row too narrow
static_assert(!CtxFitsPipeline<eff::ColdInitCtx, S_bg_int_to_int>);                        // Init ctx does not admit Bg

using FanOutGraph = StageGraph<
    StagePack<S_int_to_int, S_int_to_int, S_int_to_int, S_int_to_int>,
    EdgePack<StageEdge<0, 1>, StageEdge<0, 2>, StageEdge<0, 3>>>;
using DiamondGraph = StageGraph<
    StagePack<S_int_to_int, S_int_to_int, S_int_to_int, S_int_to_int>,
    EdgePack<StageEdge<0, 1>, StageEdge<0, 2>,
             StageEdge<1, 3>, StageEdge<2, 3>>>;
using CycleGraph = StageGraph<
    StagePack<S_int_to_int, S_int_to_int>,
    EdgePack<StageEdge<1, 0>>>;
using UnreachableGraph = StageGraph<
    StagePack<S_int_to_int, S_int_to_int, S_int_to_int>,
    EdgePack<StageEdge<0, 1>>>;
using DisconnectedGraph = StageGraph<
    StagePack<S_int_to_int, S_int_to_int, S_int_to_int, S_int_to_int>,
    EdgePack<StageEdge<0, 1>, StageEdge<2, 3>>>;

static_assert( StageGraphWellFormed<FanOutGraph>);
static_assert( StageGraphWellFormed<DiamondGraph>);
static_assert(!StageGraphWellFormed<CycleGraph>);
static_assert(!StageGraphWellFormed<UnreachableGraph>);
static_assert(!StageGraphWellFormed<DisconnectedGraph>);
static_assert( CtxFitsPipelineDag<eff::HotFgCtx, FanOutGraph>);
static_assert( CtxFitsPipelineDag<eff::HotFgCtx, DiamondGraph>);
static_assert(!CtxFitsPipelineDag<eff::HotFgCtx, CycleGraph>);
static_assert(!CtxFitsPipelineDag<eff::HotFgCtx, UnreachableGraph>);
static_assert(eff::Subrow<stage_graph_row_union_t<FanOutGraph>, eff::Row<>>);

// Pipeline<...> type-level invariants.
using P1 = Pipeline<S_int_to_int>;
using P2 = Pipeline<S_int_to_int, S_int_to_int>;
using P3 = Pipeline<S_int_to_int, S_int_to_float, S_float_to_double>;
using PDiamond = PipelineDag<DiamondGraph>;

static_assert(P1::arity == 1);
static_assert(P2::arity == 2);
static_assert(P3::arity == 3);
static_assert(PDiamond::arity == 4);
static_assert(PDiamond::edge_count == 4);
static_assert(stage_per_call_ws_known_v<S_int_to_int>);
static_assert(stage_per_call_ws_v<S_int_to_int> == 128);
static_assert(aggregate_per_call_ws_v<S_int_to_int, S_int_to_int> == 256);
static_assert(P3::aggregate_per_call_working_set == 384);
static_assert(!stage_inline_safe_v<S_int_to_int>);
static_assert(!P3::inline_safe);

// Move-only enforcement.
static_assert(!std::is_copy_constructible_v<P1>);
static_assert(!std::is_copy_assignable_v<P1>);
static_assert( std::is_move_constructible_v<P1>);
static_assert( std::is_move_assignable_v<P1>);

}  // namespace detail::pipeline_self_test

}  // namespace crucible::concurrent
