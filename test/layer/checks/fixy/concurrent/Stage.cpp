// The compile-time checks of fixy/concurrent/Stage.h.

#include <fixy/concurrent/Stage.h>

namespace fixy::concurrent {

namespace detail::stage_self_test {

namespace eff = ::foundation::effects;

using namespace ::fixy::concurrent::detail::stage_witness;

static_assert(is_consumer_handle_v<FakeConsumer<int>>);
static_assert(is_producer_handle_v<FakeProducer<int>>);
static_assert(is_swmr_writer_v<FakeWriter<int>>);

static_assert(PipelineStage<&stage_pass_through>);

static_assert(PipelineStage<&stage_transform_int_to_float>);

using BgPayload = eff::Computation<eff::Row<eff::Effect::Bg>, int>;
using IoPayload = eff::Computation<eff::Row<eff::Effect::IO>, int>;
using AllocCapPayload = eff::Capability<eff::Effect::Alloc, eff::Bg>;

inline void stage_bg_input(FakeConsumer<BgPayload>&&, FakeProducer<int>&&) noexcept {}
inline void stage_io_output(FakeConsumer<int>&&, FakeProducer<IoPayload>&&) noexcept {}
inline void stage_alloc_cap_input(FakeConsumer<AllocCapPayload>&&, FakeProducer<int>&&) noexcept {}

static_assert(PipelineStage<&stage_bg_input>);
static_assert(PipelineStage<&stage_io_output>);
static_assert(PipelineStage<&stage_alloc_cap_input>);

inline void stage_wrong_arity(FakeConsumer<int>&&) noexcept {}
inline int stage_returns_int(FakeConsumer<int>&&, FakeProducer<int>&&) noexcept { return 0; }
inline void stage_wrong_param_kind(FakeConsumer<int>&, FakeProducer<int>&&) noexcept {}

static_assert(!PipelineStage<&stage_wrong_arity>);
static_assert(!PipelineStage<&stage_returns_int>);
static_assert(!PipelineStage<&stage_wrong_param_kind>);

static_assert(CtxFitsStage<&stage_pass_through, HotFgCtx>);
static_assert(CtxFitsStage<&stage_pass_through, BgDrainCtx>);
static_assert(CtxFitsStage<&stage_transform_int_to_float, HotFgCtx>);
static_assert(CtxFitsStage<&stage_bg_input, BgDrainCtx>);
static_assert(CtxFitsStage<&stage_io_output, BgCompileCtx>);
static_assert(CtxFitsStage<&stage_alloc_cap_input, BgDrainCtx>);
static_assert(!CtxFitsStage<&stage_wrong_arity, HotFgCtx>);
static_assert(!CtxFitsStage<&stage_returns_int, HotFgCtx>);
static_assert(!CtxFitsStage<&stage_pass_through, int>);
static_assert(!CtxFitsStage<&stage_bg_input, HotFgCtx>);
static_assert(!CtxFitsStage<&stage_io_output, HotFgCtx>);
static_assert(!CtxFitsStage<&stage_io_output, BgDrainCtx>);
static_assert(!CtxFitsStage<&stage_alloc_cap_input, HotFgCtx>);

using S1 = Stage<&stage_pass_through, HotFgCtx>;

static_assert(std::is_same_v<typename S1::ctx_type, HotFgCtx>);
static_assert(std::is_same_v<typename S1::consumer_handle_type, FakeConsumer<int>>);
static_assert(std::is_same_v<typename S1::producer_handle_type, FakeProducer<int>>);
static_assert(std::is_same_v<typename S1::input_value_type, int>);
static_assert(std::is_same_v<typename S1::output_value_type, int>);
static_assert(S1::is_value_preserving);
static_assert(S1::fn_ptr == &stage_pass_through);

using S2 = Stage<&stage_transform_int_to_float, BgDrainCtx>;
static_assert(std::is_same_v<typename S2::input_value_type, int>);
static_assert(std::is_same_v<typename S2::output_value_type, float>);
static_assert(!S2::is_value_preserving);

static_assert(!std::is_copy_constructible_v<S1>);
static_assert(!std::is_copy_assignable_v<S1>);
static_assert(std::is_move_constructible_v<S1>);
static_assert(std::is_move_assignable_v<S1>);

// A row mismatch has to be visible to substitution.  Because the gate is in
// the requires-clause and not in the body, the probe below is well formed and
// false rather than a hard error, and these witnesses pin that.

template <auto FnPtr, class Ctx>
concept MintStageCallable =
    requires(Ctx const& probe_ctx, std::remove_reference_t<::foundation::reflect::param_type_t<FnPtr, 0>>&& probe_in,
             std::remove_reference_t<::foundation::reflect::param_type_t<FnPtr, 1>>&& probe_out) {
        { ::fixy::concurrent::mint_stage<FnPtr>(probe_ctx, std::move(probe_in), std::move(probe_out)) };
    };

static_assert(MintStageCallable<&stage_pass_through, HotFgCtx>);
static_assert(MintStageCallable<&stage_bg_input, BgDrainCtx>);
static_assert(!MintStageCallable<&stage_bg_input, HotFgCtx>);
static_assert(!MintStageCallable<&stage_io_output, HotFgCtx>);
static_assert(MintStageCallable<&stage_pass_through, HotFgCtx> == CtxFitsStage<&stage_pass_through, HotFgCtx>);
static_assert(MintStageCallable<&stage_bg_input, HotFgCtx> == CtxFitsStage<&stage_bg_input, HotFgCtx>);

// The two other stage shapes, each instantiated here and not only gated.
// A fan-in body with two consumer handles and one producer handle, and a
// body that publishes into a single-writer cell.

static_assert(VariadicPipelineStage<&stage_fan_in_two>);
static_assert(!PipelineStage<&stage_fan_in_two>);
static_assert(CtxFitsVariadicStage<&stage_fan_in_two, HotFgCtx>);
static_assert(!CtxFitsVariadicStage<&stage_wrong_arity, HotFgCtx>);
static_assert(VariadicStageHandlesMatch<&stage_fan_in_two, std::tuple<FakeConsumer<int>, FakeConsumer<int>>,
                                        std::tuple<FakeProducer<int>>>);
static_assert(
    !VariadicStageHandlesMatch<&stage_fan_in_two, std::tuple<FakeConsumer<int>>, std::tuple<FakeProducer<int>>>);
static_assert(!VariadicStageHandlesMatch<&stage_fan_in_two, std::tuple<FakeConsumer<int>, FakeConsumer<float>>,
                                         std::tuple<FakeProducer<int>>>);

using M1 = MpmcStage<&stage_fan_in_two, HotFgCtx, std::tuple<FakeConsumer<int>, FakeConsumer<int>>,
                     std::tuple<FakeProducer<int>>>;
static_assert(M1::input_count == 2);
static_assert(M1::output_count == 1);
static_assert(std::is_same_v<typename M1::template input_value_type<1>, int>);
static_assert(std::is_same_v<typename M1::template output_value_type<0>, int>);
static_assert(M1::aggregate_working_set_known);
static_assert(M1::aggregate_per_call_working_set == 192);
static_assert(!std::is_copy_constructible_v<M1>);
static_assert(std::is_move_constructible_v<M1>);

static_assert(SwmrPublishStageBody<&stage_swmr_publish>);
static_assert(!PipelineStage<&stage_swmr_publish>);
static_assert(!SwmrPublishStageBody<&stage_pass_through>);
static_assert(CtxFitsSwmrPublishStage<&stage_swmr_publish, HotFgCtx>);
static_assert(std::is_same_v<swmr_stage_row_union_t<&stage_swmr_publish>, eff::Row<>>);

using W1 = SwmrStage<&stage_swmr_publish, HotFgCtx>;
static_assert(std::is_same_v<typename W1::input_value_type, int>);
static_assert(std::is_same_v<typename W1::output_value_type, int>);
static_assert(W1::is_value_preserving);
static_assert(W1::aggregate_working_set_known);
static_assert(W1::aggregate_per_call_working_set == 128);
static_assert(!std::is_copy_constructible_v<W1>);
static_assert(std::is_move_constructible_v<W1>);

}  // namespace detail::stage_self_test

}  // namespace fixy::concurrent
