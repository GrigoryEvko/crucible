#pragma once

// Running the stages one after another on the calling thread would deadlock.
// Each stage body is a drain loop: it keeps taking from the channel behind it
// until that channel closes, so the first stage never returns, and the stage
// that would close its channel never starts.  A thread per stage is therefore
// the normal path, and the threads are joined by the array that holds them
// when run returns.
//
// A stage can opt out of that, but only if its body is a single bounded call
// rather than a drain loop, and only if the working sets are known and small
// enough to stay in one core's private cache.  Splitting bounded work across
// threads at that size costs more than it saves.  ParallelismRule answers the
// size question, so this header holds no cache threshold of its own.
//
// This suits a few long-lived stages.  Many short tasks want a work queue
// instead, since each run here pays for creating and joining the threads.
//
// Every stage settled its own fit with its own context when it was minted, and
// each of its endpoints did the same before that.  What is left to check here
// is that each output feeds the next input on one channel, and that the
// coordinating context can start threads and admits the effects of all the
// stage contexts it is about to start.  The mint checks the context of the
// minting thread, and run checks the context of the thread that starts the
// stages.
//
// A channel is known by its type.  Two channels of one type are one channel
// to the check, so each channel of a pipeline names a tag of its own.

#include <fixy/Ctx.h>
#include <fixy/concurrent/HandleTraits.h>
#include <fixy/concurrent/ParallelismRule.h>
#include <fixy/concurrent/Stage.h>
#include <fixy/concurrent/Topology.h>
#include <fixy/concurrent/WorkingSet.h>
#include <foundation/Platform.h>
#include <foundation/contracts/Decide.h>
#include <foundation/diag/RowHash.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Row.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#if __has_include(<pthread.h>) && __has_include(<sched.h>)
#include <pthread.h>
#include <sched.h>
#include <fixy/os/Sched.h>
#define CRUCIBLE_PIPELINE_HAS_PTHREAD_AFFINITY 1
#else
#define CRUCIBLE_PIPELINE_HAS_PTHREAD_AFFINITY 0
#endif

// The claims the carriers in this header make that no lattice grades.
// foundation/diag/RowHash.h folds each identity, so every carrier here
// takes a cache slot of its own rather than the zero a bare payload has.
namespace fixy::row_discipline {
struct pipeline;
template <typename Edges>
struct pipeline_dag;
}  // namespace fixy::row_discipline

namespace fixy::concurrent {

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
    using stage_type = Stage<FnPtr, Ctx>;
    static constexpr std::size_t input_count = 1;
    static constexpr std::size_t output_count = 1;

    template <std::size_t I>
        requires(I == 0)
    using input_value_type = typename stage_type::input_value_type;

    template <std::size_t I>
        requires(I == 0)
    using output_value_type = typename stage_type::output_value_type;

    template <std::size_t I>
        requires(I == 0)
    using input_handle_type = typename stage_type::consumer_handle_type;

    template <std::size_t I>
        requires(I == 0)
    using output_handle_type = typename stage_type::producer_handle_type;
};

template <auto FnPtr, class Ctx, class Inputs, class Outputs>
struct stage_ports<MpmcStage<FnPtr, Ctx, Inputs, Outputs>> {
    using stage_type = MpmcStage<FnPtr, Ctx, Inputs, Outputs>;
    static constexpr std::size_t input_count = stage_type::input_count;
    static constexpr std::size_t output_count = stage_type::output_count;

    template <std::size_t I>
        requires(I < input_count)
    using input_value_type = typename stage_type::template input_value_type<I>;

    template <std::size_t I>
        requires(I < output_count)
    using output_value_type = typename stage_type::template output_value_type<I>;

    template <std::size_t I>
        requires(I < input_count)
    using input_handle_type = typename stage_type::template input_handle_type<I>;

    template <std::size_t I>
        requires(I < output_count)
    using output_handle_type = typename stage_type::template output_handle_type<I>;
};

template <auto FnPtr, class Ctx>
struct stage_ports<SwmrStage<FnPtr, Ctx>> {
    using stage_type = SwmrStage<FnPtr, Ctx>;
    static constexpr std::size_t input_count = 1;
    static constexpr std::size_t output_count = 1;

    template <std::size_t I>
        requires(I == 0)
    using input_value_type = typename stage_type::input_value_type;

    template <std::size_t I>
        requires(I == 0)
    using output_value_type = typename stage_type::output_value_type;

    template <std::size_t I>
        requires(I == 0)
    using input_handle_type = typename stage_type::consumer_handle_type;

    template <std::size_t I>
        requires(I == 0)
    using output_handle_type = typename stage_type::writer_handle_type;
};

}  // namespace detail

template <class T>
concept IsStage = detail::is_stage<std::remove_cvref_t<T>>::value;

template <class Stage>
    requires IsStage<Stage>
inline constexpr std::size_t stage_input_count_v = detail::stage_ports<std::remove_cvref_t<Stage>>::input_count;

template <class Stage>
    requires IsStage<Stage>
inline constexpr std::size_t stage_output_count_v = detail::stage_ports<std::remove_cvref_t<Stage>>::output_count;

template <class Stage, std::size_t I>
    requires IsStage<Stage>
using stage_input_value_t = typename detail::stage_ports<std::remove_cvref_t<Stage>>::template input_value_type<I>;

template <class Stage, std::size_t I>
    requires IsStage<Stage>
using stage_output_value_t = typename detail::stage_ports<std::remove_cvref_t<Stage>>::template output_value_type<I>;

template <class Stage, std::size_t I>
    requires IsStage<Stage>
using stage_input_handle_t = typename detail::stage_ports<std::remove_cvref_t<Stage>>::template input_handle_type<I>;

template <class Stage, std::size_t I>
    requires IsStage<Stage>
using stage_output_handle_t = typename detail::stage_ports<std::remove_cvref_t<Stage>>::template output_handle_type<I>;

// An output feeds an input when the two carry one payload and their handles
// name one channel.  The payload alone is not sufficient.  A stage that
// writes channel A, before a stage that drains channel B, agrees in payload,
// no value goes from one to the other, and the pipeline never ends.  The
// caller checks the two port indices first.
template <class From, std::size_t Output, class To, std::size_t Input>
concept stage_port_feeds =
    std::is_same_v<stage_output_value_t<From, Output>, stage_input_value_t<To, Input>>
    && HandlesShareChannel<stage_output_handle_t<From, Output>, stage_input_handle_t<To, Input>>;

template <class S1, class S2>
concept stages_chain = IsStage<S1> && IsStage<S2> && stage_output_count_v<S1> == 1 && stage_input_count_v<S2> == 1
                    && stage_port_feeds<S1, 0, S2, 0>;

namespace detail {

template <class Tuple, std::size_t... Is>
consteval bool pipeline_chain_check(std::index_sequence<Is...>) noexcept {
    if constexpr (sizeof...(Is) == 0) {
        return true;  // one stage: no adjacent pair to check
    } else {
        return ((stages_chain<std::tuple_element_t<Is, Tuple>, std::tuple_element_t<Is + 1, Tuple>>) && ...);
    }
}

}  // namespace detail

template <class... Stages>
concept pipeline_chain = sizeof...(Stages) >= 1 && (IsStage<Stages> && ...)
                      && detail::pipeline_chain_check<std::tuple<std::remove_cvref_t<Stages>...>>(
                             std::make_index_sequence<(sizeof...(Stages) > 0 ? sizeof...(Stages) - 1 : 0)>{});

// The context here is the one that starts the stages, not the one any stage
// runs under.  It has to admit the effects of all of them together.

namespace detail {

template <class... Stages>
struct pipeline_row_union_impl;

template <>
struct pipeline_row_union_impl<> {
    using type = ::foundation::effects::Row<>;
};

template <class Stage0, class... Rest>
struct pipeline_row_union_impl<Stage0, Rest...> {
    using stage_row = typename std::remove_cvref_t<Stage0>::ctx_type::row_type;
    using rest_row = typename pipeline_row_union_impl<Rest...>::type;
    using type = ::foundation::effects::row_union_t<stage_row, rest_row>;
};

}  // namespace detail

template <class... Stages>
using pipeline_row_union_t = typename detail::pipeline_row_union_impl<std::remove_cvref_t<Stages>...>::type;

// A pipeline starts one thread per stage and joins them, unless the working
// sets are small enough to run inline, and that answer comes from the cache
// sizes probed at run time.  So the context that mints or runs a pipeline
// must own the authority to start threads in every case: Bg or Init, the
// same authority that fixy/os/CpuPinned.h asks of a pin.  The foreground
// hot path owns neither, and a pipeline never starts from it.
template <class Ctx>
concept CtxStartsStageThreads =
    ::foundation::effects::CtxOwnsAnyOf<Ctx, ::foundation::effects::Effect::Bg, ::foundation::effects::Effect::Init>;

template <class Ctx, class... Stages>
concept CtxFitsPipeline = ::foundation::effects::IsExecCtx<Ctx> && CtxStartsStageThreads<Ctx> && pipeline_chain<Stages...>
                       && ::foundation::decide::row_subset<pipeline_row_union_t<Stages...>, typename Ctx::row_type>();

template <class... Stages>
struct StagePack {};

template <class... Edges>
struct EdgePack {};

template <std::size_t From, std::size_t To, std::size_t FromOutput = 0, std::size_t ToInput = 0>
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

template <std::size_t From, std::size_t To, std::size_t FromOutput, std::size_t ToInput>
struct is_stage_edge<StageEdge<From, To, FromOutput, ToInput>> : std::true_type {};

template <class T>
struct is_stage_graph : std::false_type {};

template <class... Stages, class... Edges>
struct is_stage_graph<StageGraph<StagePack<Stages...>, EdgePack<Edges...>>> : std::true_type {};

template <class Graph>
struct stage_graph_traits;

template <class... Stages, class... Edges>
struct stage_graph_traits<StageGraph<StagePack<Stages...>, EdgePack<Edges...>>> {
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
        using from_stage = std::tuple_element_t<Edge::from, typename traits::stage_tuple>;
        using to_stage = std::tuple_element_t<Edge::to, typename traits::stage_tuple>;

        if constexpr (Edge::from_output >= stage_output_count_v<from_stage>
                      || Edge::to_input >= stage_input_count_v<to_stage>) {
            return false;
        } else {
            return stage_port_feeds<from_stage, Edge::from_output, to_stage, Edge::to_input>;
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
    return stage_graph_connected_impl<Graph>(typename traits::edge_pack_type{});
}

template <class Graph, std::size_t... Is>
consteval bool stage_graph_all_stages(std::index_sequence<Is...>) noexcept {
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
        } else if constexpr (!stage_graph_all_stages<Graph>(std::make_index_sequence<traits::stage_count>{})) {
            return false;
        } else if constexpr (!stage_graph_all_edges_valid<Graph>(typename traits::edge_pack_type{})) {
            return false;
        } else {
            return stage_graph_connected<Graph>();
        }
    }
}

template <class Graph>
struct stage_graph_row_union;

template <class... Stages, class... Edges>
struct stage_graph_row_union<StageGraph<StagePack<Stages...>, EdgePack<Edges...>>> {
    using type = pipeline_row_union_t<Stages...>;
};

}  // namespace detail

template <class T>
concept IsStageEdge = detail::is_stage_edge<std::remove_cvref_t<T>>::value;

template <class T>
concept IsStageGraph = detail::is_stage_graph<std::remove_cvref_t<T>>::value;

template <class Graph>
concept StageGraphWellFormed = IsStageGraph<Graph> && detail::stage_graph_well_formed<std::remove_cvref_t<Graph>>();

template <class Graph>
    requires StageGraphWellFormed<Graph>
using stage_graph_row_union_t = typename detail::stage_graph_row_union<std::remove_cvref_t<Graph>>::type;

template <class Ctx, class Graph>
concept CtxFitsPipelineDag =
    ::foundation::effects::IsExecCtx<Ctx> && CtxStartsStageThreads<Ctx> && StageGraphWellFormed<Graph>
    && ::foundation::decide::row_subset<stage_graph_row_union_t<Graph>, typename Ctx::row_type>();

template <class Stage>
struct is_stage_inline_safe : std::false_type {};

template <class Stage>
inline constexpr bool is_stage_inline_safe_v = is_stage_inline_safe<std::remove_cvref_t<Stage>>::value;

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
            return HasStaticPerCallWorkingSet<In> && HasStaticPerCallWorkingSet<Out>;
        }
    }();

    static constexpr std::size_t value = [] consteval {
        if constexpr (requires { S::aggregate_per_call_working_set; }) {
            return S::aggregate_per_call_working_set;
        } else if constexpr (known) {
            using In = typename S::consumer_handle_type;
            using Out = typename S::producer_handle_type;
            return saturating_ws_add(per_call_working_set_of_v<In>, per_call_working_set_of_v<Out>);
        } else {
            return unknown_per_call_working_set;
        }
    }();
};

template <class... Stages>
[[nodiscard]] consteval std::size_t aggregate_stage_ws() noexcept {
    std::size_t total = 0;
    ((total = saturating_ws_add(total, stage_working_set_traits<std::remove_cvref_t<Stages>>::value)), ...);
    return total;
}

}  // namespace detail

template <class Stage>
inline constexpr bool stage_per_call_ws_known_v = detail::stage_working_set_traits<std::remove_cvref_t<Stage>>::known;

template <class Stage>
inline constexpr std::size_t stage_per_call_ws_v = detail::stage_working_set_traits<std::remove_cvref_t<Stage>>::value;

template <class... Stages>
inline constexpr bool aggregate_per_call_ws_known_v = (stage_per_call_ws_known_v<Stages> && ...);

template <class... Stages>
inline constexpr std::size_t aggregate_per_call_ws_v = detail::aggregate_stage_ws<std::remove_cvref_t<Stages>...>();

template <class... Stages>
inline constexpr bool is_pipeline_inline_safe_v = (is_stage_inline_safe_v<Stages> && ...);

enum class PipelineDispatchKind : std::uint8_t {
    Inline,
    ThreadPerStage,
};

namespace detail {

// One cursor for all pipelines of the process, in each shared library too.
// Each threaded run takes the next N positions, so two runs at the same time
// start on different CPUs.
CRUCIBLE_PROCESS_WIDE inline constinit std::atomic<std::size_t> pipeline_cpu_cursor_{0};

// The CPUs that the calling thread can run on, in increasing order.  A core
// that the process must not use, such as a reserved or an isolated core, is
// outside that mask, so no stage goes there.  An unreadable mask gives no
// CPU, and the stages then run unpinned.  The context proves that the caller
// can start threads, which is the authority that a pin asks for.
template <class Ctx>
    requires CtxStartsStageThreads<Ctx>
[[nodiscard]] std::vector<int> caller_allowed_cpus_(Ctx const& /*ctx*/) noexcept {
    std::vector<int> cpus;
#if CRUCIBLE_PIPELINE_HAS_PTHREAD_AFFINITY
    cpu_set_t mask;
    CPU_ZERO(&mask);
    if (::sched_getaffinity(0, sizeof(mask), &mask) != 0) {  // SYSCALL-CAP-OK: CtxStartsStageThreads gate (Bg|Init)
        return cpus;
    }
    for (std::size_t cpu = 0; cpu < CPU_SETSIZE; ++cpu) {
        if (CPU_ISSET(cpu, &mask)) cpus.push_back(static_cast<int>(cpu));
    }
#endif
    return cpus;
}

// The CPUs that one threaded run of N stages takes, one for each stage, or
// -1 for a stage that runs unpinned.  The candidates are the CPUs of the
// cache cluster that holds the most CPUs of the caller's mask, so the stages
// of one run share one last-level cache.  When no cluster meets the mask,
// the candidates are the whole mask.  The work is O(C log C) for C CPUs in
// the mask, once for each run, next to the start of N threads.
template <std::size_t N, class Ctx>
    requires CtxStartsStageThreads<Ctx>
[[nodiscard]] std::array<int, N> pipeline_affinity_cpus_(Ctx const& ctx) noexcept {
    std::array<int, N> cpus{};
    cpus.fill(-1);

    const std::vector<int> allowed = caller_allowed_cpus_(ctx);
    if (allowed.empty()) return cpus;

    std::vector<int> candidates;
    for (auto const& cluster : Topology::instance().cache_clusters()) {
        std::vector<int> inside;
        for (int cpu : cluster) {
            if (std::binary_search(allowed.begin(), allowed.end(), cpu)) inside.push_back(cpu);
        }
        if (inside.size() > candidates.size()) candidates = std::move(inside);
    }
    if (candidates.empty()) candidates = allowed;

    const std::size_t start = pipeline_cpu_cursor_.fetch_add(N, std::memory_order_acq_rel);
    for (std::size_t i = 0; i < N; ++i) {
        cpus[i] = candidates[(start + i) % candidates.size()];
    }
    return cpus;
}

// The pin goes through the context-gated surface rather than the raw system
// call, and presents the context the stage was minted under.  Only a
// background or startup context is admitted there, so nothing on the hot path
// can repin a running thread, and a stage minted under the foreground context
// runs its worker unpinned rather than under a context it does not hold.  No
// background context is built from nothing here, because that is a forgery.
// A negative cpu does nothing, and a refused pin is tolerated.
template <class Ctx>
void pin_current_pipeline_thread_(Ctx const& ctx, int cpu) noexcept {
#if CRUCIBLE_PIPELINE_HAS_PTHREAD_AFFINITY
    if constexpr (::fixy::sched::CtxFitsRuntimeAffinity<Ctx>) {
        (void)::fixy::sched::apply_affinity_to_cpu(ctx, cpu);
    } else {
        (void)ctx;
        (void)cpu;
    }
#else
    (void)ctx;
    (void)cpu;
#endif
}

// What a linear pipeline and a graph do after their gate holds.  The two
// differ in their gate and in the claim that they fold into the row hash, and
// in nothing that runs, so each one holds its stages in this class as a
// private base.  The constructor is public: a caller that holds the stages
// can already run each one, so this class gives no power that the stages do
// not give.
template <class... Stages>
class StageRunner {
public:
    static constexpr std::size_t arity = sizeof...(Stages);
    static constexpr std::size_t aggregate_per_call_working_set = aggregate_per_call_ws_v<Stages...>;
    static constexpr bool inline_safe = is_pipeline_inline_safe_v<Stages...>;
    static constexpr bool aggregate_working_set_known = aggregate_per_call_ws_known_v<Stages...>;

    static_assert(((!is_stage_inline_safe_v<Stages> || stage_per_call_ws_known_v<Stages>) && ...),
                  "Pipeline inline opt-in requires both stage handles to expose "
                  "static constexpr per_call_working_set");

    [[nodiscard]] explicit constexpr StageRunner(Stages&&... stages) noexcept
        : stages_{std::forward<Stages>(stages)...} {}

    StageRunner(StageRunner const&) = delete("the stages hold linear endpoint handles");
    StageRunner& operator=(StageRunner const&) = delete("the stages hold linear endpoint handles");
    StageRunner(StageRunner&&) noexcept = default;
    StageRunner& operator=(StageRunner&&) noexcept = default;

    [[nodiscard]] static PipelineDispatchKind dispatch_kind() noexcept {
        static const PipelineDispatchKind kind = compute_dispatch_kind_();
        return kind;
    }

    [[nodiscard]] static bool will_run_inline() noexcept { return dispatch_kind() == PipelineDispatchKind::Inline; }

    // The same question as will_run_inline, asked of a stated cache size
    // instead of the probed one, so a caller can assert at compile time that
    // its pipeline runs inline on the target it is built for.  The runtime
    // answer stays authoritative: on a host smaller than the assertion
    // assumed, dispatch quietly falls back to a thread per stage and only the
    // claim was wrong.  A branching graph sums the working set over every
    // stage, which is the worst case of one token reaching all of them, so a
    // wide fan-out counts each branch.
    template <std::size_t L1dBytes, std::size_t L2Bytes = L1dBytes>
    [[nodiscard]] static consteval bool will_run_inline_v() noexcept {
        if constexpr (!inline_safe || !aggregate_working_set_known) {
            return false;
        } else {
            return (aggregate_per_call_working_set <= L1dBytes) || (aggregate_per_call_working_set <= L2Bytes);
        }
    }

    template <std::size_t I>
        requires(I < arity)
    [[nodiscard]] constexpr auto& stage() & noexcept {
        return std::get<I>(stages_);
    }

    template <std::size_t I>
        requires(I < arity)
    [[nodiscard]] constexpr auto const& stage() const& noexcept {
        return std::get<I>(stages_);
    }

    // The class that holds this runner checks the context of the caller
    // against its own gate before it calls here.  The context also gives the
    // authority to read the affinity mask of the caller.
    template <class Ctx>
        requires CtxStartsStageThreads<Ctx>
    void run_stages(Ctx const& ctx) && noexcept {
        if (will_run_inline()) {
            std::move(*this).run_inline_(std::index_sequence_for<Stages...>{});
        } else {
            std::move(*this).run_threaded_(ctx, std::index_sequence_for<Stages...>{});
        }
    }

private:
    [[nodiscard]] static PipelineDispatchKind compute_dispatch_kind_() noexcept {
        if constexpr (inline_safe && aggregate_working_set_known) {
            if (ParallelismRule::is_core_resident(aggregate_per_call_working_set)) {
                return PipelineDispatchKind::Inline;
            }
        }
        return PipelineDispatchKind::ThreadPerStage;
    }

    template <std::size_t... Is>
    void run_inline_(std::index_sequence<Is...>) && noexcept {
        ((void)std::move(std::get<Is>(stages_)).run(), ...);
    }

    // The CPUs come from the mask of the caller at each run.  Two pipelines
    // of one size then do not share their cores, and a mask that the caller
    // sets after the first run still applies.
    template <class Ctx, std::size_t... Is>
    void run_threaded_(Ctx const& ctx, std::index_sequence<Is...>) && noexcept {
        const std::array<int, sizeof...(Is)> affinity_cpus = pipeline_affinity_cpus_<sizeof...(Is)>(ctx);

        // Each thread takes its stage by move.  The array destructor at the
        // end of this function joins them all.
        [[maybe_unused]] std::array<std::jthread, sizeof...(Is)> threads = {std::jthread{
            [stage = std::move(std::get<Is>(stages_)), cpu = affinity_cpus[Is]](std::stop_token) mutable noexcept {
                pin_current_pipeline_thread_(stage.ctx(), cpu);
                std::move(stage).run();
            }}...};
    }

    std::tuple<Stages...> stages_;
};

}  // namespace detail

template <class... Stages>
    requires pipeline_chain<Stages...>
class Pipeline : private detail::StageRunner<Stages...> {
    using runner_type = detail::StageRunner<Stages...>;

public:
    using row_discipline = ::fixy::row_discipline::pipeline;
    using row_payload = ::foundation::diag::row_payloads<Stages...>;
    using runner_type::aggregate_per_call_working_set;
    using runner_type::aggregate_working_set_known;
    using runner_type::arity;
    using runner_type::dispatch_kind;
    using runner_type::inline_safe;
    using runner_type::stage;
    using runner_type::will_run_inline;
    using runner_type::will_run_inline_v;

    Pipeline(Pipeline const&) = delete(
        "Pipeline holds move-only Stages, each of which holds linear Permission tokens via its consumer/producer handles");
    Pipeline& operator=(Pipeline const&) = delete(
        "Pipeline holds move-only Stages, each of which holds linear Permission tokens via its consumer/producer handles");
    Pipeline(Pipeline&&) noexcept = default;
    Pipeline& operator=(Pipeline&&) noexcept = default;

    // The caller presents its own context, and the gate of the mint runs
    // again on it.  A pipeline can move to another thread after the mint,
    // so the context of the mint says nothing about the thread that starts
    // the stages.
    template <::foundation::effects::IsExecCtx RunCtx>
        requires CtxFitsPipeline<RunCtx, Stages...>
    void run(RunCtx const& ctx) && noexcept {
        static_cast<runner_type&&>(*this).run_stages(ctx);
    }

private:
    // Private because direct construction would skip the row admission and
    // produce a pipeline whose effects were never weighed against the context
    // that starts it.
    [[nodiscard]] explicit constexpr Pipeline(Stages&&... stages) noexcept
        : runner_type{std::forward<Stages>(stages)...} {}

    // The constraint must match the definition's exactly.  A friend whose
    // constraints differ declares a DIFFERENT template, so the friendship
    // attaches to nothing and the private constructor stays unreachable.
    // `mint_pipeline_dag` below keeps the same discipline against its own
    // gate.
    template <::foundation::effects::IsExecCtx MintCtx, class... MintStages>
        requires CtxFitsPipeline<MintCtx, std::remove_cvref_t<MintStages>...>
    friend constexpr auto mint_pipeline(MintCtx const&, MintStages&&...) noexcept;
};

// Declared ahead of the class below because the friend declaration inside it
// has to name this same concept.  Out of order, the friend declaration and the
// factory would not match and the factory could not reach the constructor.
namespace detail {

// Read only after CtxFitsPipelineDag holds, so Graph is a stage graph here.
template <class Graph, class... Stages>
concept graph_stage_pack_is =
    std::is_same_v<typename stage_graph_traits<std::remove_cvref_t<Graph>>::stage_pack_type,
                   StagePack<std::remove_cvref_t<Stages>...>>;

}  // namespace detail

// A conjunction and not a folded value, so a refusal names the clause that
// failed: the context, the graph, or the pack of stages.
template <class Ctx, class Graph, class... Stages>
concept CtxFitsPipelineDagMint = CtxFitsPipelineDag<Ctx, Graph> && detail::graph_stage_pack_is<Graph, Stages...>;

template <class Graph>
    requires StageGraphWellFormed<Graph>
class PipelineDag;

template <class... Stages, class... Edges>
    requires StageGraphWellFormed<StageGraph<StagePack<Stages...>, EdgePack<Edges...>>>
class PipelineDag<StageGraph<StagePack<Stages...>, EdgePack<Edges...>>> : private detail::StageRunner<Stages...> {
    using runner_type = detail::StageRunner<Stages...>;

public:
    using graph_type = StageGraph<StagePack<Stages...>, EdgePack<Edges...>>;
    // The edges are the shape of the graph, which is part of the claim;
    // the stages are what it runs.
    using row_discipline = ::fixy::row_discipline::pipeline_dag<EdgePack<Edges...>>;
    using row_payload = ::foundation::diag::row_payloads<Stages...>;
    using runner_type::aggregate_per_call_working_set;
    using runner_type::aggregate_working_set_known;
    using runner_type::arity;
    using runner_type::dispatch_kind;
    using runner_type::inline_safe;
    using runner_type::stage;
    using runner_type::will_run_inline;
    using runner_type::will_run_inline_v;

    static constexpr std::size_t edge_count = sizeof...(Edges);

    PipelineDag(PipelineDag const&) = delete("PipelineDag holds move-only Stages, each of which owns endpoint handles");
    PipelineDag&
    operator=(PipelineDag const&) = delete("PipelineDag holds move-only Stages, each of which owns endpoint handles");
    PipelineDag(PipelineDag&&) noexcept = default;
    PipelineDag& operator=(PipelineDag&&) noexcept = default;

    // As for a linear pipeline, the caller presents its own context.
    template <::foundation::effects::IsExecCtx RunCtx>
        requires CtxFitsPipelineDag<RunCtx, graph_type>
    void run(RunCtx const& ctx) && noexcept {
        static_cast<runner_type&&>(*this).run_stages(ctx);
    }

private:
    // Private because direct construction would skip the row admission and
    // produce a graph whose effects were never weighed against the context
    // that starts it.
    [[nodiscard]] explicit constexpr PipelineDag(Stages&&... stages) noexcept
        : runner_type{std::forward<Stages>(stages)...} {}

    template <::foundation::effects::IsExecCtx MintCtx, class MintGraph, class... MintStages>
        requires CtxFitsPipelineDagMint<MintCtx, MintGraph, MintStages...>
    friend constexpr auto mint_pipeline_dag(MintCtx const&, MintGraph, MintStages&&...) noexcept;
};

template <::foundation::effects::IsExecCtx Ctx, class... Stages>
    requires CtxFitsPipeline<Ctx, std::remove_cvref_t<Stages>...>
[[nodiscard]] constexpr auto mint_pipeline(Ctx const& /*ctx*/, Stages&&... stages) noexcept {
    // The requires clause checks the row, and a refused row never reaches
    // this body.  The compiler names the row conjunct of CtxFitsPipeline.
    return Pipeline<std::remove_cvref_t<Stages>...>{std::forward<Stages>(stages)...};
}

template <::foundation::effects::IsExecCtx Ctx, class Graph, class... Stages>
    requires CtxFitsPipelineDagMint<Ctx, Graph, Stages...>
[[nodiscard]] constexpr auto mint_pipeline_dag(Ctx const& /*ctx*/, Graph, Stages&&... stages) noexcept {
    using graph_type = std::remove_cvref_t<Graph>;
    return PipelineDag<graph_type>{std::forward<Stages>(stages)...};
}

namespace detail::pipeline_self_test {

namespace eff = ::foundation::effects;

using namespace ::fixy::concurrent::detail::stage_self_test;

inline void stage_transform_float_to_double(FakeConsumer<float>&&, FakeProducer<double>&&) noexcept {}
static_assert(PipelineStage<&stage_transform_float_to_double>);

using S_int_to_int = Stage<&stage_pass_through, HotFgCtx>;
using S_int_to_float = Stage<&stage_transform_int_to_float, HotFgCtx>;
using S_float_to_double = Stage<&stage_transform_float_to_double, HotFgCtx>;
using S_bg_int_to_int = Stage<&stage_pass_through, BgDrainCtx>;
using S_init_int_to_int = Stage<&stage_pass_through, ColdInitCtx>;

static_assert(IsStage<S_int_to_int>);
static_assert(IsStage<S_int_to_float>);
static_assert(IsStage<S_float_to_double>);
static_assert(!IsStage<int>);
static_assert(!IsStage<HotFgCtx>);

// The other two stage shapes read through the same ports.  These are the
// witnesses for them.
static_assert(IsStage<M1>);
static_assert(IsStage<W1>);
static_assert(stage_input_count_v<M1> == 2);
static_assert(stage_output_count_v<M1> == 1);
static_assert(std::is_same_v<stage_input_value_t<M1, 1>, int>);
static_assert(std::is_same_v<stage_output_value_t<M1, 0>, int>);
static_assert(stage_input_count_v<W1> == 1);
static_assert(stage_output_count_v<W1> == 1);
static_assert(std::is_same_v<stage_output_value_t<W1, 0>, int>);
// The writer of W1 publishes into a cell, and no stage drains a cell, so W1
// feeds no stage although the payloads agree.
static_assert(!stages_chain<W1, S_int_to_int>);
static_assert(stages_chain<M1, S_int_to_int>);
static_assert(!stages_chain<S_int_to_int, M1>);

// One payload on two channels.  The producer of this stage names another
// channel than the consumer of S_int_to_int names.
template <typename T>
struct OtherChannel {};

template <typename T>
struct OtherProducer {
    using channel_type = OtherChannel<T>;
    static constexpr std::size_t per_call_working_set = 64;
    [[nodiscard]] bool try_push(T const&) noexcept { return false; }
};

// A producer that names no channel.  It meets no consumer, and it can still
// be the last output of a pipeline, where no stage follows.
template <typename T>
struct NamelessProducer {
    static constexpr std::size_t per_call_working_set = 64;
    [[nodiscard]] bool try_push(T const&) noexcept { return false; }
};

inline void stage_into_other_channel(FakeConsumer<int>&&, OtherProducer<int>&&) noexcept {}
inline void stage_into_nameless(FakeConsumer<int>&&, NamelessProducer<int>&&) noexcept {}
using S_into_other = Stage<&stage_into_other_channel, HotFgCtx>;
using S_into_nameless = Stage<&stage_into_nameless, HotFgCtx>;

static_assert(std::is_same_v<stage_output_value_t<S_into_other, 0>, stage_input_value_t<S_int_to_int, 0>>);
static_assert(!stages_chain<S_into_other, S_int_to_int>, "one payload, two channels");
static_assert(!stages_chain<S_into_nameless, S_int_to_int>, "a handle that names no channel feeds no stage");
static_assert(pipeline_chain<S_int_to_int, S_into_nameless>);

static_assert(stages_chain<S_int_to_int, S_int_to_int>);
static_assert(stages_chain<S_int_to_float, S_float_to_double>);
static_assert(!stages_chain<S_int_to_int, S_float_to_double>);
static_assert(!stages_chain<S_int_to_float, S_int_to_int>);
static_assert(!stages_chain<int, S_int_to_int>);
static_assert(!stages_chain<S_int_to_int, int>);

static_assert(pipeline_chain<S_int_to_int>);
static_assert(pipeline_chain<S_int_to_int, S_int_to_int>);
static_assert(pipeline_chain<S_int_to_int, S_int_to_float, S_float_to_double>);
// The stages run under different contexts; only the payload types have to meet.
static_assert(pipeline_chain<S_bg_int_to_int, S_int_to_int>);
static_assert(!pipeline_chain<S_int_to_int, S_float_to_double>);
static_assert(!pipeline_chain<S_int_to_int, S_int_to_int, S_float_to_double>);
static_assert(!pipeline_chain<S_into_other, S_int_to_int>);
static_assert(!pipeline_chain<int>);
static_assert(!pipeline_chain<>);

static_assert(eff::Subrow<pipeline_row_union_t<S_int_to_int>, eff::Row<>>);
static_assert(eff::Subrow<pipeline_row_union_t<S_bg_int_to_int>, eff::Row<eff::Effect::Bg, eff::Effect::Alloc>>);
static_assert(eff::Subrow<pipeline_row_union_t<S_bg_int_to_int, S_init_int_to_int>,
                          eff::Row<eff::Effect::Bg, eff::Effect::Alloc, eff::Effect::Init, eff::Effect::IO>>);

static_assert(CtxFitsPipeline<BgDrainCtx, S_int_to_int>);
static_assert(CtxFitsPipeline<ColdInitCtx, S_int_to_int>);
static_assert(CtxFitsPipeline<BgDrainCtx, S_int_to_float, S_float_to_double>);
static_assert(CtxFitsPipeline<BgDrainCtx, S_bg_int_to_int, S_int_to_int>);
static_assert(!CtxFitsPipeline<int, S_int_to_int>);
static_assert(!CtxFitsPipeline<BgDrainCtx, S_int_to_int, S_float_to_double>);
static_assert(!CtxFitsPipeline<BgDrainCtx, int>);
// The hot context admits the empty row of the stages, and it still starts
// no pipeline, because it owns no authority to start threads.
static_assert(!CtxStartsStageThreads<HotFgCtx>);
static_assert(!CtxFitsPipeline<HotFgCtx, S_int_to_int>);
static_assert(!CtxStartsStageThreads<TestRunnerCtx>);
static_assert(CtxStartsStageThreads<BgDrainCtx> && CtxStartsStageThreads<ColdInitCtx>);
// The coordinating context admits fewer effects than the stage needs.
static_assert(!CtxFitsPipeline<ColdInitCtx, S_bg_int_to_int>);

using FanOutGraph = StageGraph<StagePack<S_int_to_int, S_int_to_int, S_int_to_int, S_int_to_int>,
                               EdgePack<StageEdge<0, 1>, StageEdge<0, 2>, StageEdge<0, 3>>>;
using DiamondGraph = StageGraph<StagePack<S_int_to_int, S_int_to_int, S_int_to_int, S_int_to_int>,
                                EdgePack<StageEdge<0, 1>, StageEdge<0, 2>, StageEdge<1, 3>, StageEdge<2, 3>>>;
using CycleGraph = StageGraph<StagePack<S_int_to_int, S_int_to_int>, EdgePack<StageEdge<1, 0>>>;
using UnreachableGraph = StageGraph<StagePack<S_int_to_int, S_int_to_int, S_int_to_int>, EdgePack<StageEdge<0, 1>>>;
using DisconnectedGraph = StageGraph<StagePack<S_int_to_int, S_int_to_int, S_int_to_int, S_int_to_int>,
                                     EdgePack<StageEdge<0, 1>, StageEdge<2, 3>>>;
// One edge, which agrees in payload and joins two channels.
using CrossedGraph = StageGraph<StagePack<S_into_other, S_int_to_int>, EdgePack<StageEdge<0, 1>>>;

static_assert(StageGraphWellFormed<FanOutGraph>);
static_assert(StageGraphWellFormed<DiamondGraph>);
static_assert(!StageGraphWellFormed<CycleGraph>);
static_assert(!StageGraphWellFormed<UnreachableGraph>);
static_assert(!StageGraphWellFormed<DisconnectedGraph>);
static_assert(!StageGraphWellFormed<CrossedGraph>);
static_assert(CtxFitsPipelineDag<BgDrainCtx, FanOutGraph>);
static_assert(CtxFitsPipelineDag<BgDrainCtx, DiamondGraph>);
static_assert(!CtxFitsPipelineDag<HotFgCtx, DiamondGraph>);
static_assert(!CtxFitsPipelineDag<BgDrainCtx, CycleGraph>);
static_assert(!CtxFitsPipelineDag<BgDrainCtx, UnreachableGraph>);
static_assert(eff::Subrow<stage_graph_row_union_t<FanOutGraph>, eff::Row<>>);

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
static_assert(!is_stage_inline_safe_v<S_int_to_int>);
static_assert(!P3::inline_safe);

static_assert(!std::is_copy_constructible_v<P1>);
static_assert(!std::is_copy_assignable_v<P1>);
static_assert(std::is_move_constructible_v<P1>);
static_assert(std::is_move_assignable_v<P1>);

}  // namespace detail::pipeline_self_test

}  // namespace fixy::concurrent
