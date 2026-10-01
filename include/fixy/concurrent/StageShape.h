#pragma once

// Recognizes the pipeline-stage signature: a void function taking one
// or more consumer handles, then one or more producer handles, each by
// non-const rvalue reference.  The order is the data-flow order, so a
// producer parameter ahead of a consumer parameter is not a stage.
//
// The shape this header recognizes is the shape of a stage, so the
// header sits beside the stage.

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
    ::foundation::reflect::arity_v<FnPtr> >= 2 && std::is_void_v<::foundation::reflect::return_type_t<FnPtr>>
    && StageArity<FnPtr>::ordered && StageArity<FnPtr>::input_count > 0 && StageArity<FnPtr>::output_count > 0
    && StageArity<FnPtr>::input_count + StageArity<FnPtr>::output_count == ::foundation::reflect::arity_v<FnPtr>;

template <auto FnPtr>
concept PipelineStage =
    VariadicPipelineStage<FnPtr> && StageArity<FnPtr>::input_count == 1 && StageArity<FnPtr>::output_count == 1;

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

}  // namespace fixy::concurrent
