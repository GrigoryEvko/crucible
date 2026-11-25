// The compile-time checks of fixy/concurrent/StageShape.h.

#include <fixy/concurrent/StageShape.h>

namespace fixy::concurrent {

namespace detail::stage_shape_self_test {

template <typename T>
struct fake_consumer {
    [[nodiscard]] ::foundation::core::Option<T> try_pop() noexcept { return {}; }
};

template <typename T>
struct fake_producer {
    [[nodiscard]] bool try_push(T const&) noexcept { return true; }
};

inline void f_nullary() noexcept {}
static_assert(!PipelineStage<&f_nullary>);

inline void f_one_int(int) noexcept {}
static_assert(!PipelineStage<&f_one_int>);

inline void f_two_ints(int, int) noexcept {}
static_assert(!PipelineStage<&f_two_ints>);

inline void f_three_params(int, int, int) noexcept {}
static_assert(!PipelineStage<&f_three_params>);

inline void f_one_to_one(fake_consumer<int>&&, fake_producer<int>&&) noexcept {}
static_assert(VariadicPipelineStage<&f_one_to_one>);
static_assert(PipelineStage<&f_one_to_one>);
static_assert(StageArity<&f_one_to_one>::input_count == 1);
static_assert(StageArity<&f_one_to_one>::output_count == 1);

inline void f_three_to_one(fake_consumer<int>&&, fake_consumer<int>&&, fake_consumer<float>&&,
                           fake_producer<int>&&) noexcept {}
static_assert(VariadicPipelineStage<&f_three_to_one>);
static_assert(!PipelineStage<&f_three_to_one>);
static_assert(StageArity<&f_three_to_one>::input_count == 3);
static_assert(StageArity<&f_three_to_one>::output_count == 1);
static_assert(std::is_same_v<pipeline_stage_input_value_at_t<&f_three_to_one, 2>, float>);
static_assert(std::is_same_v<pipeline_stage_output_value_at_t<&f_three_to_one, 0>, int>);

inline void f_one_to_two(fake_consumer<int>&&, fake_producer<int>&&, fake_producer<float>&&) noexcept {}
static_assert(VariadicPipelineStage<&f_one_to_two>);
static_assert(!PipelineStage<&f_one_to_two>);
static_assert(StageArity<&f_one_to_two>::input_count == 1);
static_assert(StageArity<&f_one_to_two>::output_count == 2);
static_assert(std::is_same_v<pipeline_stage_output_value_at_t<&f_one_to_two, 1>, float>);

inline void f_interleaved(fake_consumer<int>&&, fake_producer<int>&&, fake_consumer<int>&&) noexcept {}
static_assert(!VariadicPipelineStage<&f_interleaved>);
static_assert(!PipelineStage<&f_interleaved>);

// A stage whose parameters are the right handles but taken by lvalue
// reference or by value is not a stage: the body consumes its handles,
// and a stage that did not would leave the caller holding a moved-from
// endpoint.  The shape alone decides it, so the witness is here and
// not on CtxFitsStage.
inline void f_lvalue_in(fake_consumer<int>&, fake_producer<int>&&) noexcept {}
inline void f_const_rvalue_in(fake_consumer<int> const&&, fake_producer<int>&&) noexcept {}
inline void f_by_value_in(fake_consumer<int>, fake_producer<int>&&) noexcept {}
static_assert(!PipelineStage<&f_lvalue_in>);
static_assert(!PipelineStage<&f_const_rvalue_in>);
static_assert(!PipelineStage<&f_by_value_in>);

// A non-void return is not a stage.  The body's whole output goes
// through its producer handles, so a returned value would be a second
// output channel that nothing drains.
inline int f_returns_int(fake_consumer<int>&&, fake_producer<int>&&) noexcept { return 0; }
static_assert(!PipelineStage<&f_returns_int>);

// ── the context of a stage ──────────────────────────────────────────

template <typename T>
struct ctx_consumer {
    template <class Ctx>
    [[nodiscard]] ::foundation::core::Option<T> try_pop(Ctx const&) noexcept {
        return {};
    }
};

template <typename T>
struct ctx_producer {
    using value_type = T;
    template <class Ctx>
    [[nodiscard]] bool try_push(Ctx const&, T const&) noexcept {
        return true;
    }
};

using WideCtx = ::fixy::BgLoadCtx;
using NarrowCtx = ::fixy::BgDrainCtx;

// A body of plain handles can name its context.
inline void f_plain_with_ctx(fake_consumer<int>&&, fake_producer<int>&&, NarrowCtx const&) noexcept {}
static_assert(PipelineStage<&f_plain_with_ctx>);
static_assert(StageArity<&f_plain_with_ctx>::ctx_count == 1 && StageArity<&f_plain_with_ctx>::handle_count == 2);
static_assert(StageBodyTakesCtx<&f_plain_with_ctx, NarrowCtx> && !StageBodyTakesCtx<&f_plain_with_ctx, WideCtx>);
static_assert(StageBodyTakesCtx<&f_one_to_one, NarrowCtx> && StageBodyTakesCtx<&f_one_to_one, WideCtx>,
              "a body that names no context runs in each context");

// A body of a handle of the context form names its context.
inline void f_ctx_handles(ctx_consumer<int>&&, ctx_producer<float>&&, WideCtx const&) noexcept {}
inline void f_ctx_input(ctx_consumer<int>&&, fake_producer<int>&&, WideCtx const&) noexcept {}
inline void f_ctx_output(fake_consumer<int>&&, ctx_producer<int>&&, WideCtx const&) noexcept {}
static_assert(PipelineStage<&f_ctx_handles> && PipelineStage<&f_ctx_input> && PipelineStage<&f_ctx_output>);
static_assert(StageArity<&f_ctx_handles>::ctx_handle_count == 2 && StageArity<&f_ctx_input>::ctx_handle_count == 1);
static_assert(std::is_same_v<pipeline_stage_input_value_t<&f_ctx_handles>, int>);
static_assert(std::is_same_v<pipeline_stage_output_value_t<&f_ctx_handles>, float>);
inline void f_ctx_handles_no_ctx(ctx_consumer<int>&&, ctx_producer<int>&&) noexcept {}
inline void f_ctx_output_no_ctx(fake_consumer<int>&&, ctx_producer<int>&&) noexcept {}
static_assert(!StageNamesCtxOfItsHandles<&f_ctx_handles_no_ctx> && !PipelineStage<&f_ctx_handles_no_ctx>);
static_assert(!StageNamesCtxOfItsHandles<&f_ctx_output_no_ctx> && !PipelineStage<&f_ctx_output_no_ctx>);

// The context comes one time, after the handles and by reference to const.
inline void f_ctx_first(WideCtx const&, fake_consumer<int>&&, fake_producer<int>&&) noexcept {}
inline void f_ctx_between(fake_consumer<int>&&, WideCtx const&, fake_producer<int>&&) noexcept {}
inline void f_two_ctx(fake_consumer<int>&&, fake_producer<int>&&, WideCtx const&, WideCtx const&) noexcept {}
inline void f_ctx_by_value(fake_consumer<int>&&, fake_producer<int>&&, WideCtx) noexcept {}
inline void f_ctx_by_mutable(fake_consumer<int>&&, fake_producer<int>&&, WideCtx&) noexcept {}
static_assert(!PipelineStage<&f_ctx_first> && !PipelineStage<&f_ctx_between> && !PipelineStage<&f_two_ctx>);
static_assert(!PipelineStage<&f_ctx_by_value> && !PipelineStage<&f_ctx_by_mutable>);

static_assert(pipeline_stage_is_value_preserving_v<&f_one_to_one>);

inline void f_int_to_float(fake_consumer<int>&&, fake_producer<float>&&) noexcept {}
static_assert(PipelineStage<&f_int_to_float>);
static_assert(!pipeline_stage_is_value_preserving_v<&f_int_to_float>);
static_assert(std::is_same_v<pipeline_stage_input_value_t<&f_int_to_float>, int>);
static_assert(std::is_same_v<pipeline_stage_output_value_t<&f_int_to_float>, float>);

}  // namespace detail::stage_shape_self_test

}  // namespace fixy::concurrent
