#pragma once

#include <crucible/safety/ConsumerEndpoint.h>
#include <crucible/safety/_OwnedRegion.h>
#include <crucible/safety/PipelineStage.h>
#include <crucible/safety/ProducerEndpoint.h>

#include <cstddef>
#include <optional>
#include <type_traits>

namespace crucible::fixy::wrap {

using ::crucible::safety::extract::StageArity;
using ::crucible::safety::extract::VariadicPipelineStage;
using ::crucible::safety::extract::PipelineStage;
using ::crucible::safety::extract::is_pipeline_stage_v;
using ::crucible::safety::extract::pipeline_stage_input_value_at_t;
using ::crucible::safety::extract::pipeline_stage_output_value_at_t;
using ::crucible::safety::extract::pipeline_stage_input_value_t;
using ::crucible::safety::extract::pipeline_stage_output_value_t;
using ::crucible::safety::extract::pipeline_stage_is_value_preserving_v;

using ::crucible::safety::extract::ConsumerEndpoint;
using ::crucible::safety::extract::is_consumer_endpoint_v;
using ::crucible::safety::extract::consumer_endpoint_handle_value_t;
using ::crucible::safety::extract::consumer_endpoint_region_tag_t;
using ::crucible::safety::extract::consumer_endpoint_region_value_t;
using ::crucible::safety::extract::consumer_endpoint_value_consistent_v;

using ::crucible::safety::extract::ProducerEndpoint;
using ::crucible::safety::extract::is_producer_endpoint_v;
using ::crucible::safety::extract::producer_endpoint_handle_value_t;
using ::crucible::safety::extract::producer_endpoint_region_tag_t;
using ::crucible::safety::extract::producer_endpoint_region_value_t;
using ::crucible::safety::extract::producer_endpoint_value_consistent_v;

}  // namespace crucible::fixy::wrap

namespace crucible::fixy::wrap::self_test_pipeline_stage_endpoint {

struct ProbeTagA {};
struct ProbeTagB {};

template <typename T>
struct probe_consumer_handle {
    [[nodiscard]] std::optional<T> try_pop() noexcept { return {}; }
};

template <typename T>
struct probe_producer_handle {
    [[nodiscard]] bool try_push(T const&) noexcept { return true; }
};

inline void f_stage_int_int(probe_consumer_handle<int>&&, probe_producer_handle<int>&&) noexcept {}

inline void f_stage_int_float(probe_consumer_handle<int>&&, probe_producer_handle<float>&&) noexcept {}

inline void f_consumer_endpoint_int(probe_consumer_handle<int>&&,
                                    ::crucible::safety::OwnedRegion<int, ProbeTagA>&&) noexcept {}

inline void f_consumer_endpoint_mismatch(probe_consumer_handle<int>&&,
                                         ::crucible::safety::OwnedRegion<double, ProbeTagA>&&) noexcept {}

inline void f_producer_endpoint_int(probe_producer_handle<int>&&,
                                    ::crucible::safety::OwnedRegion<int, ProbeTagB>&&) noexcept {}

inline void f_producer_endpoint_mismatch(probe_producer_handle<int>&&,
                                         ::crucible::safety::OwnedRegion<double, ProbeTagB>&&) noexcept {}

inline void f_two_ints(int, int) noexcept {}

static_assert(::crucible::fixy::wrap::StageArity<&f_stage_int_int>::input_count
              == ::crucible::safety::extract::StageArity<&f_stage_int_int>::input_count);
static_assert(::crucible::fixy::wrap::StageArity<&f_stage_int_int>::input_count == 1);
static_assert(::crucible::fixy::wrap::StageArity<&f_stage_int_int>::output_count == 1);
static_assert(::crucible::fixy::wrap::StageArity<&f_stage_int_int>::ordered == true);

inline void f_stage_2to1_for_arity(probe_consumer_handle<int>&&, probe_consumer_handle<float>&&,
                                   probe_producer_handle<int>&&) noexcept {}
static_assert(::crucible::fixy::wrap::StageArity<&f_stage_2to1_for_arity>::input_count == 2);
static_assert(::crucible::fixy::wrap::StageArity<&f_stage_2to1_for_arity>::output_count == 1);
static_assert(::crucible::fixy::wrap::StageArity<&f_stage_2to1_for_arity>::ordered == true);

inline void f_stage_unordered(probe_producer_handle<int>&&, probe_consumer_handle<int>&&) noexcept {}
static_assert(::crucible::fixy::wrap::StageArity<&f_stage_unordered>::ordered == false);

static_assert(::crucible::fixy::wrap::PipelineStage<&f_stage_int_int>);
static_assert(::crucible::fixy::wrap::PipelineStage<&f_stage_int_float>);
static_assert(!::crucible::fixy::wrap::PipelineStage<&f_two_ints>);
static_assert(!::crucible::fixy::wrap::PipelineStage<&f_consumer_endpoint_int>);
static_assert(!::crucible::fixy::wrap::PipelineStage<&f_producer_endpoint_int>);

static_assert(::crucible::fixy::wrap::VariadicPipelineStage<&f_stage_int_int>);
static_assert(!::crucible::fixy::wrap::VariadicPipelineStage<&f_two_ints>);

// The cross-path equalities are not tautologies. Each spelling is a separate
// atomic-constraint evaluation, so a declaration in this namespace that
// shadows the re-export makes the two sides diverge.
static_assert(::crucible::fixy::wrap::PipelineStage<&f_stage_int_int>
              == ::crucible::safety::extract::PipelineStage<&f_stage_int_int>);
static_assert(::crucible::fixy::wrap::PipelineStage<&f_two_ints>
              == ::crucible::safety::extract::PipelineStage<&f_two_ints>);
static_assert(::crucible::fixy::wrap::VariadicPipelineStage<&f_stage_int_int>
              == ::crucible::safety::extract::VariadicPipelineStage<&f_stage_int_int>);

static_assert(::crucible::fixy::wrap::is_pipeline_stage_v<&f_stage_int_int>
              == ::crucible::safety::extract::is_pipeline_stage_v<&f_stage_int_int>);
static_assert(::crucible::fixy::wrap::is_pipeline_stage_v<&f_stage_int_int> == true);
static_assert(::crucible::fixy::wrap::is_pipeline_stage_v<&f_two_ints> == false);

static_assert(std::is_same_v<::crucible::fixy::wrap::pipeline_stage_input_value_t<&f_stage_int_int>,
                             ::crucible::safety::extract::pipeline_stage_input_value_t<&f_stage_int_int>>);
static_assert(std::is_same_v<::crucible::fixy::wrap::pipeline_stage_input_value_t<&f_stage_int_int>, int>);
static_assert(std::is_same_v<::crucible::fixy::wrap::pipeline_stage_output_value_t<&f_stage_int_int>, int>);
static_assert(std::is_same_v<::crucible::fixy::wrap::pipeline_stage_output_value_t<&f_stage_int_float>, float>);

static_assert(std::is_same_v<::crucible::fixy::wrap::pipeline_stage_input_value_at_t<&f_stage_int_int, 0>,
                             ::crucible::safety::extract::pipeline_stage_input_value_at_t<&f_stage_int_int, 0>>);
static_assert(std::is_same_v<::crucible::fixy::wrap::pipeline_stage_input_value_at_t<&f_stage_int_int, 0>, int>);
static_assert(std::is_same_v<::crucible::fixy::wrap::pipeline_stage_output_value_at_t<&f_stage_int_float, 0>, float>);

static_assert(::crucible::fixy::wrap::pipeline_stage_is_value_preserving_v<&f_stage_int_int>
              == ::crucible::safety::extract::pipeline_stage_is_value_preserving_v<&f_stage_int_int>);
static_assert(::crucible::fixy::wrap::pipeline_stage_is_value_preserving_v<&f_stage_int_int> == true);
static_assert(::crucible::fixy::wrap::pipeline_stage_is_value_preserving_v<&f_stage_int_float> == false);

static_assert(::crucible::fixy::wrap::ConsumerEndpoint<&f_consumer_endpoint_int>);
static_assert(::crucible::fixy::wrap::ConsumerEndpoint<&f_consumer_endpoint_mismatch>);
static_assert(!::crucible::fixy::wrap::ConsumerEndpoint<&f_two_ints>);
static_assert(!::crucible::fixy::wrap::ConsumerEndpoint<&f_stage_int_int>);
static_assert(!::crucible::fixy::wrap::ConsumerEndpoint<&f_producer_endpoint_int>);

static_assert(::crucible::fixy::wrap::ConsumerEndpoint<&f_consumer_endpoint_int>
              == ::crucible::safety::extract::ConsumerEndpoint<&f_consumer_endpoint_int>);
static_assert(::crucible::fixy::wrap::ConsumerEndpoint<&f_two_ints>
              == ::crucible::safety::extract::ConsumerEndpoint<&f_two_ints>);

static_assert(::crucible::fixy::wrap::is_consumer_endpoint_v<&f_consumer_endpoint_int>
              == ::crucible::safety::extract::is_consumer_endpoint_v<&f_consumer_endpoint_int>);
static_assert(::crucible::fixy::wrap::is_consumer_endpoint_v<&f_consumer_endpoint_int> == true);
static_assert(::crucible::fixy::wrap::is_consumer_endpoint_v<&f_two_ints> == false);

static_assert(std::is_same_v<::crucible::fixy::wrap::consumer_endpoint_handle_value_t<&f_consumer_endpoint_int>,
                             ::crucible::safety::extract::consumer_endpoint_handle_value_t<&f_consumer_endpoint_int>>);
static_assert(std::is_same_v<::crucible::fixy::wrap::consumer_endpoint_handle_value_t<&f_consumer_endpoint_int>, int>);
static_assert(
    std::is_same_v<::crucible::fixy::wrap::consumer_endpoint_region_tag_t<&f_consumer_endpoint_int>, ProbeTagA>);
static_assert(std::is_same_v<::crucible::fixy::wrap::consumer_endpoint_region_value_t<&f_consumer_endpoint_int>, int>);
static_assert(
    std::is_same_v<::crucible::fixy::wrap::consumer_endpoint_region_value_t<&f_consumer_endpoint_mismatch>, double>);

static_assert(::crucible::fixy::wrap::consumer_endpoint_value_consistent_v<&f_consumer_endpoint_int>
              == ::crucible::safety::extract::consumer_endpoint_value_consistent_v<&f_consumer_endpoint_int>);
static_assert(::crucible::fixy::wrap::consumer_endpoint_value_consistent_v<&f_consumer_endpoint_int> == true);
static_assert(::crucible::fixy::wrap::consumer_endpoint_value_consistent_v<&f_consumer_endpoint_mismatch> == false);

static_assert(::crucible::fixy::wrap::ProducerEndpoint<&f_producer_endpoint_int>);
static_assert(::crucible::fixy::wrap::ProducerEndpoint<&f_producer_endpoint_mismatch>);
static_assert(!::crucible::fixy::wrap::ProducerEndpoint<&f_two_ints>);
static_assert(!::crucible::fixy::wrap::ProducerEndpoint<&f_stage_int_int>);
static_assert(!::crucible::fixy::wrap::ProducerEndpoint<&f_consumer_endpoint_int>);

static_assert(::crucible::fixy::wrap::ProducerEndpoint<&f_producer_endpoint_int>
              == ::crucible::safety::extract::ProducerEndpoint<&f_producer_endpoint_int>);
static_assert(::crucible::fixy::wrap::ProducerEndpoint<&f_two_ints>
              == ::crucible::safety::extract::ProducerEndpoint<&f_two_ints>);

static_assert(::crucible::fixy::wrap::is_producer_endpoint_v<&f_producer_endpoint_int>
              == ::crucible::safety::extract::is_producer_endpoint_v<&f_producer_endpoint_int>);
static_assert(::crucible::fixy::wrap::is_producer_endpoint_v<&f_producer_endpoint_int> == true);
static_assert(::crucible::fixy::wrap::is_producer_endpoint_v<&f_two_ints> == false);

static_assert(std::is_same_v<::crucible::fixy::wrap::producer_endpoint_handle_value_t<&f_producer_endpoint_int>,
                             ::crucible::safety::extract::producer_endpoint_handle_value_t<&f_producer_endpoint_int>>);
static_assert(std::is_same_v<::crucible::fixy::wrap::producer_endpoint_handle_value_t<&f_producer_endpoint_int>, int>);
static_assert(
    std::is_same_v<::crucible::fixy::wrap::producer_endpoint_region_tag_t<&f_producer_endpoint_int>, ProbeTagB>);
static_assert(std::is_same_v<::crucible::fixy::wrap::producer_endpoint_region_value_t<&f_producer_endpoint_int>, int>);
static_assert(
    std::is_same_v<::crucible::fixy::wrap::producer_endpoint_region_value_t<&f_producer_endpoint_mismatch>, double>);

static_assert(::crucible::fixy::wrap::producer_endpoint_value_consistent_v<&f_producer_endpoint_int>
              == ::crucible::safety::extract::producer_endpoint_value_consistent_v<&f_producer_endpoint_int>);
static_assert(::crucible::fixy::wrap::producer_endpoint_value_consistent_v<&f_producer_endpoint_int> == true);
static_assert(::crucible::fixy::wrap::producer_endpoint_value_consistent_v<&f_producer_endpoint_mismatch> == false);

static_assert(::crucible::fixy::wrap::PipelineStage<&f_stage_int_int>);
static_assert(!::crucible::fixy::wrap::ConsumerEndpoint<&f_stage_int_int>);
static_assert(!::crucible::fixy::wrap::ProducerEndpoint<&f_stage_int_int>);

static_assert(!::crucible::fixy::wrap::PipelineStage<&f_consumer_endpoint_int>);
static_assert(::crucible::fixy::wrap::ConsumerEndpoint<&f_consumer_endpoint_int>);
static_assert(!::crucible::fixy::wrap::ProducerEndpoint<&f_consumer_endpoint_int>);

static_assert(!::crucible::fixy::wrap::PipelineStage<&f_producer_endpoint_int>);
static_assert(!::crucible::fixy::wrap::ConsumerEndpoint<&f_producer_endpoint_int>);
static_assert(::crucible::fixy::wrap::ProducerEndpoint<&f_producer_endpoint_int>);

constexpr int pipeline_stage_endpoint_alias_cardinality = 21;
static_assert(pipeline_stage_endpoint_alias_cardinality == 21,
              "fixy::wrap pipeline-stage and endpoint alias cardinality changed "
              "— update the using-decls AND this sentinel in lockstep.");

}  // namespace crucible::fixy::wrap::self_test_pipeline_stage_endpoint
