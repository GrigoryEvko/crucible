#pragma once

// Recognizes the pipeline-stage signature: a void function taking one
// or more consumer handles, then one or more producer handles, each by
// non-const rvalue reference.  The order is the data-flow order, so a
// producer parameter ahead of a consumer parameter is not a stage.
//
// Old spelling: include/crucible/safety/_PipelineStage.h, namespace
// crucible::safety::extract.  The shape it recognizes is a stage's, so
// it lands beside the stage rather than in a namespace named for the
// act of reading it.  The name changed with the home: the concept
// PipelineStage kept its name, and the header no longer carries the
// concept's name, because sessions/SessionPatterns.h also declares a
// PipelineStage and the two are different things.

#include <fixy/concurrent/HandleTraits.h>
#include <foundation/reflect/Signature.h>

#include <cstddef>
#include <optional>
#include <type_traits>
#include <utility>

namespace fixy::concurrent {

namespace detail {

namespace refl = ::foundation::reflect;

template <auto FnPtr, std::size_t I>
inline constexpr bool stage_param_rvalue_nonconst_v =
    std::is_rvalue_reference_v<refl::param_type_t<FnPtr, I>>
    && !std::is_const_v<std::remove_reference_t<refl::param_type_t<FnPtr, I>>>;

template <auto FnPtr, std::size_t I>
inline constexpr bool stage_input_param_v =
    stage_param_rvalue_nonconst_v<FnPtr, I> && is_consumer_handle_v<refl::param_type_t<FnPtr, I>>;

template <auto FnPtr, std::size_t I>
inline constexpr bool stage_output_param_v =
    stage_param_rvalue_nonconst_v<FnPtr, I> && is_producer_handle_v<refl::param_type_t<FnPtr, I>>;

struct stage_arity_counts {
    std::size_t input_count = 0;
    std::size_t output_count = 0;
    bool ordered = true;
};

template <auto FnPtr, std::size_t I>
consteval void consume_stage_param(stage_arity_counts& counts, bool& seen_output) noexcept {
    if constexpr (stage_input_param_v<FnPtr, I>) {
        if (seen_output) counts.ordered = false;
        ++counts.input_count;
    } else if constexpr (stage_output_param_v<FnPtr, I>) {
        seen_output = true;
        ++counts.output_count;
    } else {
        counts.ordered = false;
    }
}

template <auto FnPtr, std::size_t... Is>
consteval stage_arity_counts compute_stage_arity(std::index_sequence<Is...>) noexcept {
    stage_arity_counts counts{};
    bool seen_output = false;
    (consume_stage_param<FnPtr, Is>(counts, seen_output), ...);
    return counts;
}

}  // namespace detail

template <auto FnPtr>
struct StageArity {
private:
    static constexpr detail::stage_arity_counts counts =
        detail::compute_stage_arity<FnPtr>(std::make_index_sequence<::foundation::reflect::arity_v<FnPtr>>{});

public:
    static constexpr std::size_t input_count = counts.input_count;
    static constexpr std::size_t output_count = counts.output_count;
    static constexpr bool ordered = counts.ordered;
};

template <auto FnPtr>
concept VariadicPipelineStage =
    ::foundation::reflect::arity_v<FnPtr> >= 2
    && std::is_void_v<::foundation::reflect::return_type_t<FnPtr>> && StageArity<FnPtr>::ordered
    && StageArity<FnPtr>::input_count > 0 && StageArity<FnPtr>::output_count > 0
    && StageArity<FnPtr>::input_count + StageArity<FnPtr>::output_count == ::foundation::reflect::arity_v<FnPtr>;

template <auto FnPtr>
concept PipelineStage =
    VariadicPipelineStage<FnPtr> && StageArity<FnPtr>::input_count == 1 && StageArity<FnPtr>::output_count == 1;

template <auto FnPtr>
inline constexpr bool is_pipeline_stage_v = PipelineStage<FnPtr>;

template <auto FnPtr, std::size_t I>
    requires VariadicPipelineStage<FnPtr> && (I < StageArity<FnPtr>::input_count)
using pipeline_stage_input_value_at_t = consumer_handle_value_t<::foundation::reflect::param_type_t<FnPtr, I>>;

template <auto FnPtr, std::size_t I>
    requires VariadicPipelineStage<FnPtr> && (I < StageArity<FnPtr>::output_count)
using pipeline_stage_output_value_at_t =
    producer_handle_value_t<::foundation::reflect::param_type_t<FnPtr, StageArity<FnPtr>::input_count + I>>;

template <auto FnPtr>
    requires PipelineStage<FnPtr>
using pipeline_stage_input_value_t = pipeline_stage_input_value_at_t<FnPtr, 0>;

template <auto FnPtr>
    requires PipelineStage<FnPtr>
using pipeline_stage_output_value_t = pipeline_stage_output_value_at_t<FnPtr, 0>;

template <auto FnPtr>
    requires PipelineStage<FnPtr>
inline constexpr bool pipeline_stage_is_value_preserving_v =
    std::is_same_v<pipeline_stage_input_value_t<FnPtr>, pipeline_stage_output_value_t<FnPtr>>;

namespace detail::stage_shape_self_test {

template <typename T>
struct fake_consumer {
    [[nodiscard]] std::optional<T> try_pop() noexcept { return {}; }
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
// endpoint.  The old tree pinned this on CtxFitsStage in the Stage
// header; the shape alone decides it, so it is pinned here.
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

static_assert(pipeline_stage_is_value_preserving_v<&f_one_to_one>);

inline void f_int_to_float(fake_consumer<int>&&, fake_producer<float>&&) noexcept {}
static_assert(PipelineStage<&f_int_to_float>);
static_assert(!pipeline_stage_is_value_preserving_v<&f_int_to_float>);
static_assert(std::is_same_v<pipeline_stage_input_value_t<&f_int_to_float>, int>);
static_assert(std::is_same_v<pipeline_stage_output_value_t<&f_int_to_float>, float>);

}  // namespace detail::stage_shape_self_test

}  // namespace fixy::concurrent
