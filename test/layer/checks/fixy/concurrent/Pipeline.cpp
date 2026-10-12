// The compile-time checks of fixy/concurrent/Pipeline.h.

#include <fixy/concurrent/Pipeline.h>

namespace fixy::concurrent {

namespace detail::pipeline_self_test {

namespace eff = ::foundation::effects;

using namespace ::fixy::concurrent::detail::stage_witness;

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
using M1 = MpmcStage<&stage_fan_in_two, HotFgCtx>;
using W1 = SwmrStage<&stage_swmr_publish, HotFgCtx>;
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

// A producer on the channel of S_int_to_int that reports its identity.  The
// consumer of S_int_to_int hides it, so no mint could compare the two.
template <typename T>
struct ReportingProducer {
    using channel_type = FakeChannel<T>;
    static constexpr std::size_t per_call_working_set = 64;
    [[nodiscard]] bool try_push(T const&) noexcept { return false; }
    [[nodiscard]] ::foundation::ChannelIdentity<FakeChannel<T>> channel_identity() const noexcept { return {}; }
};

inline void stage_into_reporting(FakeConsumer<int>&&, ReportingProducer<int>&&) noexcept {}
using S_into_reporting = Stage<&stage_into_reporting, HotFgCtx>;
static_assert(!stages_chain<S_into_reporting, S_int_to_int>, "an identity that one side hides is not compared");

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

// A fan-out body with one consumer handle and two producer handles.  With
// the fan-in stage M1, it makes the two branches of a diamond.
inline void stage_fan_out_two(FakeConsumer<int>&&, FakeProducer<int>&&, FakeProducer<int>&&) noexcept {}
using O1 = MpmcStage<&stage_fan_out_two, HotFgCtx>;

// The first stage and the last stage are the ends of a graph, and each
// other port has exactly one edge.  A chain of plain stages and a diamond
// through a fan-out and a fan-in hold that.
using OneStageGraph = StageGraph<StagePack<S_int_to_int>, EdgePack<>>;
using ChainGraph =
    StageGraph<StagePack<S_int_to_int, S_int_to_int, S_int_to_int>, EdgePack<StageEdge<0, 1>, StageEdge<1, 2>>>;
using DiamondGraph =
    StageGraph<StagePack<O1, S_int_to_int, S_int_to_int, M1>,
               EdgePack<StageEdge<0, 1, 0, 0>, StageEdge<0, 2, 1, 0>, StageEdge<1, 3, 0, 0>, StageEdge<2, 3, 0, 1>>>;

// The diamond through one port at each end: the output of stage 0 has two
// edges, and the input of stage 3 has two edges.
using SharedPortGraph = StageGraph<StagePack<S_int_to_int, S_int_to_int, S_int_to_int, S_int_to_int>,
                                   EdgePack<StageEdge<0, 1>, StageEdge<0, 2>, StageEdge<1, 3>, StageEdge<2, 3>>>;
using InputWithoutEdgeGraph = StageGraph<StagePack<S_int_to_int, M1>, EdgePack<StageEdge<0, 1, 0, 0>>>;
using InputWithTwoEdgesGraph =
    StageGraph<StagePack<O1, S_int_to_int>, EdgePack<StageEdge<0, 1, 0, 0>, StageEdge<0, 1, 1, 0>>>;
using OutputWithoutEdgeGraph = StageGraph<StagePack<O1, S_int_to_int>, EdgePack<StageEdge<0, 1, 0, 0>>>;
using CycleGraph = StageGraph<StagePack<S_int_to_int, S_int_to_int>, EdgePack<StageEdge<1, 0>>>;
using UnreachableGraph = StageGraph<StagePack<S_int_to_int, S_int_to_int, S_int_to_int>, EdgePack<StageEdge<0, 1>>>;
using DisconnectedGraph = StageGraph<StagePack<S_int_to_int, S_int_to_int, S_int_to_int, S_int_to_int>,
                                     EdgePack<StageEdge<0, 1>, StageEdge<2, 3>>>;
// One edge, which agrees in payload and joins two channels.
using CrossedGraph = StageGraph<StagePack<S_into_other, S_int_to_int>, EdgePack<StageEdge<0, 1>>>;
using StageOutOfRangeGraph = StageGraph<StagePack<S_int_to_int, S_int_to_int>, EdgePack<StageEdge<0, 2>>>;
using PortOutOfRangeGraph = StageGraph<StagePack<S_int_to_int, S_int_to_int>, EdgePack<StageEdge<0, 1, 1, 0>>>;
using NotAnEdgeGraph = StageGraph<StagePack<S_int_to_int, S_int_to_int>, EdgePack<StageEdge<0, 1>, int>>;
using NotAStageGraph = StageGraph<StagePack<S_int_to_int, int>, EdgePack<StageEdge<0, 1>>>;
using EmptyGraph = StageGraph<StagePack<>, EdgePack<>>;

static_assert(StageGraphWellFormed<OneStageGraph>);
static_assert(StageGraphWellFormed<ChainGraph>);
static_assert(StageGraphWellFormed<DiamondGraph>);
static_assert(!StageGraphWellFormed<SharedPortGraph>);
static_assert(!StageGraphWellFormed<CycleGraph>);
static_assert(!StageGraphWellFormed<UnreachableGraph>);
static_assert(!StageGraphWellFormed<DisconnectedGraph>);
static_assert(!StageGraphWellFormed<CrossedGraph>);

// The record of each refusal: the fault, the stage or the edge, the port,
// and the count of edges of the port.
static_assert(detail::stage_graph_check<DiamondGraph>() == StageGraphCheck{});
static_assert(detail::stage_graph_check<SharedPortGraph>()
              == StageGraphCheck{StageGraphFault::OutputPortEdgeCount, 0, 0, 2});
static_assert(detail::stage_graph_check<InputWithoutEdgeGraph>()
              == StageGraphCheck{StageGraphFault::InputPortEdgeCount, 1, 1, 0});
static_assert(detail::stage_graph_check<InputWithTwoEdgesGraph>()
              == StageGraphCheck{StageGraphFault::InputPortEdgeCount, 1, 0, 2});
static_assert(detail::stage_graph_check<OutputWithoutEdgeGraph>()
              == StageGraphCheck{StageGraphFault::OutputPortEdgeCount, 0, 1, 0});
static_assert(detail::stage_graph_check<UnreachableGraph>()
              == StageGraphCheck{StageGraphFault::OutputPortEdgeCount, 1, 0, 0});
static_assert(detail::stage_graph_check<DisconnectedGraph>()
              == StageGraphCheck{StageGraphFault::OutputPortEdgeCount, 1, 0, 0});
static_assert(detail::stage_graph_check<CycleGraph>() == StageGraphCheck{StageGraphFault::EdgeRunsBackward, 0});
static_assert(detail::stage_graph_check<CrossedGraph>() == StageGraphCheck{StageGraphFault::EdgeAcrossChannels, 0});
static_assert(detail::stage_graph_check<StageOutOfRangeGraph>() == StageGraphCheck{StageGraphFault::EdgeOutOfRange, 0});
static_assert(detail::stage_graph_check<PortOutOfRangeGraph>() == StageGraphCheck{StageGraphFault::EdgeOutOfRange, 0});
static_assert(detail::stage_graph_check<NotAnEdgeGraph>() == StageGraphCheck{StageGraphFault::NotAnEdge, 1});
static_assert(detail::stage_graph_check<NotAStageGraph>() == StageGraphCheck{StageGraphFault::NotAStage, 1});
static_assert(detail::stage_graph_check<EmptyGraph>() == StageGraphCheck{StageGraphFault::NoStage});

static_assert(CtxFitsPipelineDag<BgDrainCtx, ChainGraph>);
static_assert(CtxFitsPipelineDag<BgDrainCtx, DiamondGraph>);
static_assert(!CtxFitsPipelineDag<HotFgCtx, DiamondGraph>);
static_assert(!CtxFitsPipelineDag<BgDrainCtx, SharedPortGraph>);
static_assert(!CtxFitsPipelineDag<BgDrainCtx, CycleGraph>);
static_assert(!CtxFitsPipelineDag<BgDrainCtx, UnreachableGraph>);
static_assert(eff::Subrow<stage_graph_row_union_t<DiamondGraph>, eff::Row<>>);

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
