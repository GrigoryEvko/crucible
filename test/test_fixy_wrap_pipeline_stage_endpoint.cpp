// Including the umbrella header is itself part of the claim.  The
// static_asserts it carries are never compiled under the project warning
// flags until some translation unit pulls it in.

#include <crucible/fixy/wrap/PipelineStageEndpoint.h>

#include <crucible/safety/ConsumerEndpoint.h>
#include <crucible/safety/OwnedRegion.h>
#include <crucible/safety/PipelineStage.h>
#include <crucible/safety/ProducerEndpoint.h>

#include <cstdio>
#include <cstdlib>
#include <optional>
#include <type_traits>

namespace fw = ::crucible::fixy::wrap;
namespace extract = ::crucible::safety::extract;
namespace safety = ::crucible::safety;

namespace probes {

struct RegionTagIn {};
struct RegionTagOut {};

template <typename T>
struct consumer_handle {
    [[nodiscard]] std::optional<T> try_pop() noexcept { return {}; }
};

template <typename T>
struct producer_handle {
    [[nodiscard]] bool try_push(T const&) noexcept { return true; }
};

// A handle carrying both operations satisfies neither handle concept.
// Each of the two concepts requires the absence of the other's operation.
struct hybrid_handle {
    [[nodiscard]] bool try_push(int const&) noexcept { return true; }
    [[nodiscard]] std::optional<int> try_pop() noexcept { return {}; }
};

using OR_int_in = safety::OwnedRegion<int, RegionTagIn>;
using OR_int_out = safety::OwnedRegion<int, RegionTagOut>;
using OR_double_in = safety::OwnedRegion<double, RegionTagIn>;

// Only the address of each probe is ever taken, so the declarations need
// no definitions.
void f_stage_int_int(consumer_handle<int>&&, producer_handle<int>&&) noexcept;
void f_stage_int_float(consumer_handle<int>&&, producer_handle<float>&&) noexcept;

void f_stage_2to1(consumer_handle<int>&&, consumer_handle<int>&&, producer_handle<int>&&) noexcept;

void f_consumer_well_formed(consumer_handle<int>&&, OR_int_out&&) noexcept;
void f_consumer_value_mismatch(consumer_handle<int>&&, OR_double_in&&) noexcept;

void f_producer_well_formed(producer_handle<int>&&, OR_int_in&&) noexcept;
void f_producer_value_mismatch(producer_handle<int>&&, OR_double_in&&) noexcept;

void f_two_ints(int, int) noexcept;
void f_handle_lvalue(consumer_handle<int>&, OR_int_out&&) noexcept;
void f_region_lvalue(consumer_handle<int>&&, OR_int_out&) noexcept;
void f_consumer_in_producer_slot(consumer_handle<int>&&, OR_int_in&&) noexcept;
void f_hybrid_in_producer_slot(hybrid_handle&&, OR_int_in&&) noexcept;
int f_int_return(producer_handle<int>&&, OR_int_in&&) noexcept;

}  // namespace probes

static_assert(fw::StageArity<&probes::f_stage_int_int>::input_count == 1);
static_assert(fw::StageArity<&probes::f_stage_int_int>::output_count == 1);
static_assert(fw::StageArity<&probes::f_stage_int_int>::ordered == true);
static_assert(fw::StageArity<&probes::f_stage_2to1>::input_count == 2);
static_assert(fw::StageArity<&probes::f_stage_2to1>::output_count == 1);

static_assert(fw::StageArity<&probes::f_stage_int_int>::input_count
              == extract::StageArity<&probes::f_stage_int_int>::input_count);

static_assert(fw::PipelineStage<&probes::f_stage_int_int>);
static_assert(!fw::PipelineStage<&probes::f_two_ints>);
static_assert(!fw::PipelineStage<&probes::f_stage_2to1>);

static_assert(fw::PipelineStage<&probes::f_stage_int_int> == extract::PipelineStage<&probes::f_stage_int_int>);

static_assert(fw::VariadicPipelineStage<&probes::f_stage_int_int>);
static_assert(fw::VariadicPipelineStage<&probes::f_stage_2to1>);
static_assert(!fw::VariadicPipelineStage<&probes::f_two_ints>);

static_assert(fw::is_pipeline_stage_v<&probes::f_stage_int_int> == true);
static_assert(fw::is_pipeline_stage_v<&probes::f_two_ints> == false);

static_assert(std::is_same_v<fw::pipeline_stage_input_value_t<&probes::f_stage_int_int>, int>);
static_assert(std::is_same_v<fw::pipeline_stage_output_value_t<&probes::f_stage_int_float>, float>);
static_assert(std::is_same_v<fw::pipeline_stage_input_value_at_t<&probes::f_stage_2to1, 1>, int>);
static_assert(std::is_same_v<fw::pipeline_stage_output_value_at_t<&probes::f_stage_2to1, 0>, int>);

static_assert(fw::pipeline_stage_is_value_preserving_v<&probes::f_stage_int_int>);
static_assert(!fw::pipeline_stage_is_value_preserving_v<&probes::f_stage_int_float>);

static_assert(fw::ConsumerEndpoint<&probes::f_consumer_well_formed>);
static_assert(fw::ConsumerEndpoint<&probes::f_consumer_value_mismatch>);
static_assert(!fw::ConsumerEndpoint<&probes::f_two_ints>);
static_assert(!fw::ConsumerEndpoint<&probes::f_stage_int_int>);
static_assert(!fw::ConsumerEndpoint<&probes::f_handle_lvalue>);
static_assert(!fw::ConsumerEndpoint<&probes::f_region_lvalue>);

static_assert(fw::ConsumerEndpoint<&probes::f_consumer_well_formed>
              == extract::ConsumerEndpoint<&probes::f_consumer_well_formed>);

static_assert(std::is_same_v<fw::consumer_endpoint_handle_value_t<&probes::f_consumer_well_formed>, int>);
static_assert(
    std::is_same_v<fw::consumer_endpoint_region_tag_t<&probes::f_consumer_well_formed>, probes::RegionTagOut>);
static_assert(std::is_same_v<fw::consumer_endpoint_region_value_t<&probes::f_consumer_well_formed>, int>);
static_assert(std::is_same_v<fw::consumer_endpoint_region_value_t<&probes::f_consumer_value_mismatch>, double>);

static_assert(fw::consumer_endpoint_value_consistent_v<&probes::f_consumer_well_formed>);
static_assert(!fw::consumer_endpoint_value_consistent_v<&probes::f_consumer_value_mismatch>);

static_assert(fw::ProducerEndpoint<&probes::f_producer_well_formed>);
static_assert(fw::ProducerEndpoint<&probes::f_producer_value_mismatch>);
static_assert(!fw::ProducerEndpoint<&probes::f_two_ints>);
static_assert(!fw::ProducerEndpoint<&probes::f_stage_int_int>);
static_assert(!fw::ProducerEndpoint<&probes::f_consumer_in_producer_slot>);
static_assert(!fw::ProducerEndpoint<&probes::f_hybrid_in_producer_slot>);
static_assert(!fw::ProducerEndpoint<&probes::f_int_return>);

static_assert(fw::ProducerEndpoint<&probes::f_producer_well_formed>
              == extract::ProducerEndpoint<&probes::f_producer_well_formed>);

static_assert(std::is_same_v<fw::producer_endpoint_handle_value_t<&probes::f_producer_well_formed>, int>);
static_assert(std::is_same_v<fw::producer_endpoint_region_tag_t<&probes::f_producer_well_formed>, probes::RegionTagIn>);
static_assert(std::is_same_v<fw::producer_endpoint_region_value_t<&probes::f_producer_well_formed>, int>);
static_assert(std::is_same_v<fw::producer_endpoint_region_value_t<&probes::f_producer_value_mismatch>, double>);

static_assert(fw::producer_endpoint_value_consistent_v<&probes::f_producer_well_formed>);
static_assert(!fw::producer_endpoint_value_consistent_v<&probes::f_producer_value_mismatch>);

static_assert(fw::PipelineStage<&probes::f_stage_int_int>);
static_assert(!fw::ConsumerEndpoint<&probes::f_stage_int_int>);
static_assert(!fw::ProducerEndpoint<&probes::f_stage_int_int>);

static_assert(!fw::PipelineStage<&probes::f_consumer_well_formed>);
static_assert(fw::ConsumerEndpoint<&probes::f_consumer_well_formed>);
static_assert(!fw::ProducerEndpoint<&probes::f_consumer_well_formed>);

static_assert(!fw::PipelineStage<&probes::f_producer_well_formed>);
static_assert(!fw::ConsumerEndpoint<&probes::f_producer_well_formed>);
static_assert(fw::ProducerEndpoint<&probes::f_producer_well_formed>);

// Every sink below is volatile.  Without it the compiler folds each
// constant predicate into the branch and no runtime read remains.
static void test_runtime_stage_arity_readback() {
    volatile std::size_t in_1x1 = fw::StageArity<&probes::f_stage_int_int>::input_count;
    volatile std::size_t out_1x1 = fw::StageArity<&probes::f_stage_int_int>::output_count;
    volatile std::size_t in_2to1 = fw::StageArity<&probes::f_stage_2to1>::input_count;
    volatile std::size_t out_2to1 = fw::StageArity<&probes::f_stage_2to1>::output_count;
    volatile bool ordered = fw::StageArity<&probes::f_stage_int_int>::ordered;
    if (in_1x1 != 1) std::abort();
    if (out_1x1 != 1) std::abort();
    if (in_2to1 != 2) std::abort();
    if (out_2to1 != 1) std::abort();
    if (!ordered) std::abort();
}

static void test_runtime_pipeline_stage_admission() {
    volatile bool ok_stage = fw::is_pipeline_stage_v<&probes::f_stage_int_int>;
    volatile bool reject_stage = fw::is_pipeline_stage_v<&probes::f_two_ints>;
    volatile bool reject_stage2 = fw::is_pipeline_stage_v<&probes::f_stage_2to1>;
    if (!ok_stage) std::abort();
    if (reject_stage) std::abort();
    if (reject_stage2) std::abort();
}

static void test_runtime_consumer_endpoint_admission() {
    volatile bool ok_consumer = fw::is_consumer_endpoint_v<&probes::f_consumer_well_formed>;
    volatile bool ok_consumer2 = fw::is_consumer_endpoint_v<&probes::f_consumer_value_mismatch>;
    volatile bool reject_consumer = fw::is_consumer_endpoint_v<&probes::f_two_ints>;
    if (!ok_consumer) std::abort();
    if (!ok_consumer2) std::abort();
    if (reject_consumer) std::abort();
}

static void test_runtime_producer_endpoint_admission() {
    volatile bool ok_producer = fw::is_producer_endpoint_v<&probes::f_producer_well_formed>;
    volatile bool ok_producer2 = fw::is_producer_endpoint_v<&probes::f_producer_value_mismatch>;
    volatile bool reject_producer = fw::is_producer_endpoint_v<&probes::f_two_ints>;
    if (!ok_producer) std::abort();
    if (!ok_producer2) std::abort();
    if (reject_producer) std::abort();
}

// The umbrella re-exports the concepts and extractors, not the smoke-test
// bodies, so these two calls go through the substrate namespace.
static void test_runtime_substrate_smoke_calls() {
    if (!extract::consumer_endpoint_smoke_test()) std::abort();
    if (!extract::producer_endpoint_smoke_test()) std::abort();
}

static void test_runtime_value_consistent_predicate() {
    volatile bool consumer_ok = fw::consumer_endpoint_value_consistent_v<&probes::f_consumer_well_formed>;
    volatile bool consumer_bad = fw::consumer_endpoint_value_consistent_v<&probes::f_consumer_value_mismatch>;
    volatile bool producer_ok = fw::producer_endpoint_value_consistent_v<&probes::f_producer_well_formed>;
    volatile bool producer_bad = fw::producer_endpoint_value_consistent_v<&probes::f_producer_value_mismatch>;
    if (!consumer_ok) std::abort();
    if (consumer_bad) std::abort();
    if (!producer_ok) std::abort();
    if (producer_bad) std::abort();
}

static void test_runtime_value_preserving_predicate() {
    volatile bool preserving = fw::pipeline_stage_is_value_preserving_v<&probes::f_stage_int_int>;
    volatile bool transforming = fw::pipeline_stage_is_value_preserving_v<&probes::f_stage_int_float>;
    if (!preserving) std::abort();
    if (transforming) std::abort();
}

static void test_runtime_variadic_vs_strict_stage() {
    volatile bool variadic_admits = fw::VariadicPipelineStage<&probes::f_stage_2to1>;
    volatile bool strict_admits = fw::PipelineStage<&probes::f_stage_2to1>;
    if (!variadic_admits) std::abort();
    if (strict_admits) std::abort();
}

static void test_runtime_cross_shape_exclusion() {
    {
        volatile bool s = fw::is_pipeline_stage_v<&probes::f_stage_int_int>;
        volatile bool c = fw::is_consumer_endpoint_v<&probes::f_stage_int_int>;
        volatile bool p = fw::is_producer_endpoint_v<&probes::f_stage_int_int>;
        if (!s) std::abort();
        if (c) std::abort();
        if (p) std::abort();
    }
    {
        volatile bool s = fw::is_pipeline_stage_v<&probes::f_consumer_well_formed>;
        volatile bool c = fw::is_consumer_endpoint_v<&probes::f_consumer_well_formed>;
        volatile bool p = fw::is_producer_endpoint_v<&probes::f_consumer_well_formed>;
        if (s) std::abort();
        if (!c) std::abort();
        if (p) std::abort();
    }
    {
        volatile bool s = fw::is_pipeline_stage_v<&probes::f_producer_well_formed>;
        volatile bool c = fw::is_consumer_endpoint_v<&probes::f_producer_well_formed>;
        volatile bool p = fw::is_producer_endpoint_v<&probes::f_producer_well_formed>;
        if (s) std::abort();
        if (c) std::abort();
        if (!p) std::abort();
    }
}

int main() {
    test_runtime_stage_arity_readback();
    test_runtime_pipeline_stage_admission();
    test_runtime_consumer_endpoint_admission();
    test_runtime_producer_endpoint_admission();
    test_runtime_substrate_smoke_calls();
    test_runtime_value_consistent_predicate();
    test_runtime_value_preserving_predicate();
    test_runtime_variadic_vs_strict_stage();
    test_runtime_cross_shape_exclusion();
    std::printf("test_fixy_wrap_pipeline_stage_endpoint: "
                "9/9 runtime witnesses passed\n");
    return 0;
}
