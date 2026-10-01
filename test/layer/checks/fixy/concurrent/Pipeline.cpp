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
using M1 = MpmcStage<&stage_fan_in_two, HotFgCtx, std::tuple<FakeConsumer<int>, FakeConsumer<int>>,
                     std::tuple<FakeProducer<int>>>;
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
