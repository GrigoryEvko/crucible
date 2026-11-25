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
static_assert(S1::fn_ptr() == &stage_pass_through);

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
static_assert(VariadicStageHandlesMatch<&stage_fan_in_two, FakeConsumer<int>, FakeConsumer<int>, FakeProducer<int>>);
static_assert(!VariadicStageHandlesMatch<&stage_fan_in_two, FakeConsumer<int>, FakeProducer<int>>);
static_assert(!VariadicStageHandlesMatch<&stage_fan_in_two, FakeConsumer<int>, FakeConsumer<float>, FakeProducer<int>>);
static_assert(!VariadicStageHandlesMatch<&stage_fan_in_two, FakeConsumer<int>, FakeProducer<int>, FakeConsumer<int>>);

// The mint takes each handle by move, with the exact type of its parameter.
static_assert(CtxFitsMpmcStage<&stage_fan_in_two, HotFgCtx, FakeConsumer<int>, FakeConsumer<int>, FakeProducer<int>>);
static_assert(!CtxFitsMpmcStage<&stage_fan_in_two, HotFgCtx, FakeConsumer<int>&, FakeConsumer<int>, FakeProducer<int>>);
static_assert(
    !CtxFitsMpmcStage<&stage_fan_in_two, HotFgCtx, FakeConsumer<int> const, FakeConsumer<int>, FakeProducer<int>>);
static_assert(!CtxFitsMpmcStage<&stage_fan_in_two, int, FakeConsumer<int>, FakeConsumer<int>, FakeProducer<int>>);

using M1 = MpmcStage<&stage_fan_in_two, HotFgCtx>;
static_assert(M1::input_count == 2);
static_assert(std::is_same_v<typename M1::template input_handle_type<1>, FakeConsumer<int>>);
static_assert(std::is_same_v<typename M1::template output_handle_type<0>, FakeProducer<int>>);
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

// The mint of the single-writer stage takes the consumer handle and the
// writer, and it gives the stage of the body.  The two negative-compile
// fixtures neg_swmr_stage_mint_* hold the refusals of its gate.
static_assert(std::is_same_v<decltype(::fixy::concurrent::mint_swmr_stage<&stage_swmr_publish>(
                                 std::declval<HotFgCtx const&>(), std::declval<FakeConsumer<int>>(),
                                 std::declval<FakeWriter<int>>())),
                             W1>);

// ── the effects of a handle ─────────────────────────────────────────
//
// A handle that reaches a context, a capability or a bare tag is no handle of
// a stage, also when the context of the stage owns the effect.

template <class Held>
class HoldingConsumer {
public:
    using channel_type = FakeChannel<int>;
    static constexpr std::size_t per_call_working_set = 64;
    static constexpr std::size_t channel_capacity = 0;
    explicit HoldingConsumer(Held held) noexcept : held_{static_cast<Held&&>(held)} {}
    [[nodiscard]] ::foundation::core::Option<int> try_pop() noexcept { return {}; }

private:
    Held held_;
};

template <class Held>
struct HoldingProducer {
    using channel_type = FakeChannel<int>;
    static constexpr std::size_t per_call_working_set = 64;
    static constexpr std::size_t channel_capacity = 0;
    Held held;
    [[nodiscard]] bool try_push(int const&) noexcept { return false; }
};

template <class Held>
struct HoldingWriter {
    using channel_type = FakeCell<int>;
    static constexpr std::size_t per_call_working_set = 64;
    Held held;
    void publish(int const&) noexcept {}
};

template <class Held>
inline void stage_of_consumer(HoldingConsumer<Held>&&, FakeProducer<int>&&) noexcept {}
template <class Held>
inline void stage_of_producer(FakeConsumer<int>&&, HoldingProducer<Held>&&) noexcept {}
template <class Held>
inline void stage_of_fan_in(FakeConsumer<int>&&, HoldingConsumer<Held>&&, FakeProducer<int>&&) noexcept {}
template <class Held>
inline void stage_of_writer(FakeConsumer<int>&&, HoldingWriter<Held>&&) noexcept {}

template <class Held, class Ctx = BgLoadCtx>
inline constexpr bool takes_held =
    CtxFitsStage<&stage_of_consumer<Held>, Ctx> && CtxFitsStage<&stage_of_producer<Held>, Ctx>
    && CtxFitsVariadicStage<&stage_of_fan_in<Held>, Ctx> && CtxFitsSwmrPublishStage<&stage_of_writer<Held>, Ctx>;
template <class Held, class Ctx = BgLoadCtx>
inline constexpr bool refuses_held =
    !StageHandlesReachNoEffect<&stage_of_consumer<Held>> && !CtxFitsStage<&stage_of_consumer<Held>, Ctx>
    && !CtxFitsStage<&stage_of_producer<Held>, Ctx> && !CtxFitsVariadicStage<&stage_of_fan_in<Held>, Ctx>
    && !CtxFitsSwmrPublishStage<&stage_of_writer<Held>, Ctx>;

// The controls: a plain member, and a context that only an argument names.
struct Plain {
    int value = 0;
};
template <class Ctx>
struct NamesCtx {
    int value = 0;
};
static_assert(takes_held<int> && takes_held<Plain> && takes_held<NamesCtx<BgLoadCtx>> && takes_held<Plain, HotFgCtx>);

struct Nested {
    Plain plain{};
    ::foundation::core::Option<eff::cap::Block> block{::foundation::core::none};
};
static_assert(refuses_held<BgLoadCtx> && refuses_held<BgDrainCtx> && refuses_held<BgLoadCtx, BgDrainCtx>);
static_assert(refuses_held<eff::cap::Alloc> && refuses_held<eff::cap::IO> && refuses_held<eff::cap::Block>
              && refuses_held<eff::cap::Nondet> && refuses_held<eff::Bg>);
static_assert(refuses_held<eff::Capability<eff::Effect::Block, eff::Bg>>);
static_assert(refuses_held<Nested> && refuses_held<eff::cap::Nondet[2]>);
// A borrow reaches the context as a value does.
struct BorrowBrand {};
static_assert(refuses_held<::foundation::core::Ref<BgLoadCtx const, BorrowBrand>> && refuses_held<BgLoadCtx const&>);
static_assert(refuses_held<::foundation::core::Box<Nested>>);

// The payload of a channel is no part of a handle: the gate of the payload
// rows weighs it.  A payload that holds a bare tag needs its effect.
class TagPayload {
public:
    explicit TagPayload(eff::cap::Block block) noexcept : block_{block} {}

private:
    eff::cap::Block block_;
};
template <class Payload>
struct PayloadConsumer {
    using channel_type = FakeChannel<Payload>;
    static constexpr std::size_t channel_capacity = 0;
    ::foundation::core::Option<Payload> cell{::foundation::core::none};
    [[nodiscard]] ::foundation::core::Option<Payload> try_pop() noexcept { return {}; }
};
inline void stage_tag_payload(PayloadConsumer<TagPayload>&&, FakeProducer<int>&&) noexcept {}
static_assert(StageHandlesReachNoEffect<&stage_tag_payload>);
static_assert(CtxFitsStage<&stage_tag_payload, BgLoadCtx> && !CtxFitsStage<&stage_tag_payload, BgDrainCtx>);

}  // namespace detail::stage_self_test

}  // namespace fixy::concurrent
