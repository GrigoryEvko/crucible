// ── test_fixy_wrap_pipeline_stage_endpoint — V-043 sentinel TU
//
// Forces evaluation of the header-internal static_asserts in
// `include/crucible/fixy/wrap/PipelineStageEndpoint.h` under the
// project's warnings-as-errors flags (per
// feedback_header_only_static_assert_blind_spot.md), and adds
// runtime witnesses on top of the compile-time identity sentinels.
//
// Covers the three dispatch-shape recognizer substrates:
//   * PipelineStage      (1×1 body shape)
//   * ConsumerEndpoint   (consumer-handle × OwnedRegion drain)
//   * ProducerEndpoint   (producer-handle × OwnedRegion publish)
//
// The umbrella header ships 13 sentinel-block sections × ~10
// static_asserts each.  This TU re-states the most load-bearing
// claims at TU scope so the verification crosses both reach paths
// (fixy::wrap → safety::extract) AND is witnessed by a runtime
// driver that returns 0 on success / aborts on first failure.

#include <crucible/fixy/wrap/PipelineStageEndpoint.h>

#include <crucible/safety/ConsumerEndpoint.h>
#include <crucible/safety/OwnedRegion.h>
#include <crucible/safety/PipelineStage.h>
#include <crucible/safety/ProducerEndpoint.h>

#include <cstdio>
#include <cstdlib>
#include <optional>
#include <type_traits>

namespace fw      = ::crucible::fixy::wrap;
namespace extract = ::crucible::safety::extract;
namespace safety  = ::crucible::safety;

// ═══════════════════════════════════════════════════════════════════
// ── Probe types ──────────────────────────────────────────────────
// ═══════════════════════════════════════════════════════════════════

namespace probes {

struct RegionTagIn  {};
struct RegionTagOut {};

template <typename T>
struct consumer_handle {
    [[nodiscard]] std::optional<T> try_pop() noexcept { return {}; }
};

template <typename T>
struct producer_handle {
    [[nodiscard]] bool try_push(T const&) noexcept { return true; }
};

// Hybrid handle (both try_push AND try_pop) — rejected by IsProducerHandle
// AND IsConsumerHandle's negative-requirement gates.
struct hybrid_handle {
    [[nodiscard]] bool try_push(int const&) noexcept { return true; }
    [[nodiscard]] std::optional<int> try_pop() noexcept { return {}; }
};

using OR_int_in    = safety::OwnedRegion<int,    RegionTagIn>;
using OR_int_out   = safety::OwnedRegion<int,    RegionTagOut>;
using OR_double_in = safety::OwnedRegion<double, RegionTagIn>;

// ── Probe function declarations (definitions not required — only
// the address-of needs to bind) ──────────────────────────────────

// PipelineStage shapes (1×1).
void f_stage_int_int(consumer_handle<int>&&, producer_handle<int>&&) noexcept;
void f_stage_int_float(consumer_handle<int>&&, producer_handle<float>&&) noexcept;

// Variadic-but-not-1×1 stage.
void f_stage_2to1(consumer_handle<int>&&,
                  consumer_handle<int>&&,
                  producer_handle<int>&&) noexcept;

// ConsumerEndpoint shapes.
void f_consumer_well_formed(consumer_handle<int>&&, OR_int_out&&) noexcept;
void f_consumer_value_mismatch(consumer_handle<int>&&, OR_double_in&&) noexcept;

// ProducerEndpoint shapes.
void f_producer_well_formed(producer_handle<int>&&, OR_int_in&&) noexcept;
void f_producer_value_mismatch(producer_handle<int>&&, OR_double_in&&) noexcept;

// Negative-shape probes.
void f_two_ints(int, int) noexcept;
void f_handle_lvalue(consumer_handle<int>&,  OR_int_out&&) noexcept;
void f_region_lvalue(consumer_handle<int>&&, OR_int_out&)  noexcept;
void f_consumer_in_producer_slot(consumer_handle<int>&&, OR_int_in&&) noexcept;
void f_hybrid_in_producer_slot(hybrid_handle&&,           OR_int_in&&) noexcept;
int  f_int_return(producer_handle<int>&&, OR_int_in&&) noexcept;

}  // namespace probes

// ═══════════════════════════════════════════════════════════════════
// ── Compile-time witnesses (re-state at TU scope) ────────────────
// ═══════════════════════════════════════════════════════════════════
//
// The umbrella header carries the identity / value / admission
// sentinels.  We re-state a representative subset here so a build-
// config divergence between the umbrella TU and this TU would catch
// it (the same blind-spot rationale that motivates V-040..V-042).

// ── 1. PipelineStage — concept admission cross-path identity ─────

static_assert( fw::PipelineStage<&probes::f_stage_int_int>);
static_assert(!fw::PipelineStage<&probes::f_two_ints>);
static_assert(!fw::PipelineStage<&probes::f_stage_2to1>);

static_assert(
    fw::PipelineStage<&probes::f_stage_int_int> ==
    extract::PipelineStage<&probes::f_stage_int_int>);

// VariadicPipelineStage admits the 1×1 AND the 2-in-1-out forms.
static_assert( fw::VariadicPipelineStage<&probes::f_stage_int_int>);
static_assert( fw::VariadicPipelineStage<&probes::f_stage_2to1>);
static_assert(!fw::VariadicPipelineStage<&probes::f_two_ints>);

// ── 2. PipelineStage — bool trait identity ───────────────────────

static_assert(fw::is_pipeline_stage_v<&probes::f_stage_int_int>  == true);
static_assert(fw::is_pipeline_stage_v<&probes::f_two_ints>       == false);

// ── 3. PipelineStage — extractor type-alias identity ─────────────

static_assert(std::is_same_v<
    fw::pipeline_stage_input_value_t<&probes::f_stage_int_int>, int>);
static_assert(std::is_same_v<
    fw::pipeline_stage_output_value_t<&probes::f_stage_int_float>, float>);
static_assert(std::is_same_v<
    fw::pipeline_stage_input_value_at_t<&probes::f_stage_2to1, 1>, int>);
static_assert(std::is_same_v<
    fw::pipeline_stage_output_value_at_t<&probes::f_stage_2to1, 0>, int>);

// ── 4. PipelineStage — is_value_preserving_v ─────────────────────

static_assert( fw::pipeline_stage_is_value_preserving_v<&probes::f_stage_int_int>);
static_assert(!fw::pipeline_stage_is_value_preserving_v<&probes::f_stage_int_float>);

// ── 5. ConsumerEndpoint — concept admission cross-path identity ──

static_assert( fw::ConsumerEndpoint<&probes::f_consumer_well_formed>);
static_assert( fw::ConsumerEndpoint<&probes::f_consumer_value_mismatch>);
static_assert(!fw::ConsumerEndpoint<&probes::f_two_ints>);
static_assert(!fw::ConsumerEndpoint<&probes::f_stage_int_int>);
static_assert(!fw::ConsumerEndpoint<&probes::f_handle_lvalue>);
static_assert(!fw::ConsumerEndpoint<&probes::f_region_lvalue>);

static_assert(
    fw::ConsumerEndpoint<&probes::f_consumer_well_formed> ==
    extract::ConsumerEndpoint<&probes::f_consumer_well_formed>);

// ── 6. ConsumerEndpoint — extractor type-alias identity ──────────

static_assert(std::is_same_v<
    fw::consumer_endpoint_handle_value_t<&probes::f_consumer_well_formed>, int>);
static_assert(std::is_same_v<
    fw::consumer_endpoint_region_tag_t<&probes::f_consumer_well_formed>,
    probes::RegionTagOut>);
static_assert(std::is_same_v<
    fw::consumer_endpoint_region_value_t<&probes::f_consumer_well_formed>, int>);
static_assert(std::is_same_v<
    fw::consumer_endpoint_region_value_t<&probes::f_consumer_value_mismatch>, double>);

// ── 7. ConsumerEndpoint — value_consistent_v ─────────────────────

static_assert( fw::consumer_endpoint_value_consistent_v<&probes::f_consumer_well_formed>);
static_assert(!fw::consumer_endpoint_value_consistent_v<&probes::f_consumer_value_mismatch>);

// ── 8. ProducerEndpoint — concept admission cross-path identity ──

static_assert( fw::ProducerEndpoint<&probes::f_producer_well_formed>);
static_assert( fw::ProducerEndpoint<&probes::f_producer_value_mismatch>);
static_assert(!fw::ProducerEndpoint<&probes::f_two_ints>);
static_assert(!fw::ProducerEndpoint<&probes::f_stage_int_int>);
static_assert(!fw::ProducerEndpoint<&probes::f_consumer_in_producer_slot>);
static_assert(!fw::ProducerEndpoint<&probes::f_hybrid_in_producer_slot>);
static_assert(!fw::ProducerEndpoint<&probes::f_int_return>);

static_assert(
    fw::ProducerEndpoint<&probes::f_producer_well_formed> ==
    extract::ProducerEndpoint<&probes::f_producer_well_formed>);

// ── 9. ProducerEndpoint — extractor type-alias identity ──────────

static_assert(std::is_same_v<
    fw::producer_endpoint_handle_value_t<&probes::f_producer_well_formed>, int>);
static_assert(std::is_same_v<
    fw::producer_endpoint_region_tag_t<&probes::f_producer_well_formed>,
    probes::RegionTagIn>);
static_assert(std::is_same_v<
    fw::producer_endpoint_region_value_t<&probes::f_producer_well_formed>, int>);
static_assert(std::is_same_v<
    fw::producer_endpoint_region_value_t<&probes::f_producer_value_mismatch>, double>);

// ── 10. ProducerEndpoint — value_consistent_v ────────────────────

static_assert( fw::producer_endpoint_value_consistent_v<&probes::f_producer_well_formed>);
static_assert(!fw::producer_endpoint_value_consistent_v<&probes::f_producer_value_mismatch>);

// ── 11. Cross-shape exclusion ────────────────────────────────────

static_assert( fw::PipelineStage<&probes::f_stage_int_int>);
static_assert(!fw::ConsumerEndpoint<&probes::f_stage_int_int>);
static_assert(!fw::ProducerEndpoint<&probes::f_stage_int_int>);

static_assert(!fw::PipelineStage<&probes::f_consumer_well_formed>);
static_assert( fw::ConsumerEndpoint<&probes::f_consumer_well_formed>);
static_assert(!fw::ProducerEndpoint<&probes::f_consumer_well_formed>);

static_assert(!fw::PipelineStage<&probes::f_producer_well_formed>);
static_assert(!fw::ConsumerEndpoint<&probes::f_producer_well_formed>);
static_assert( fw::ProducerEndpoint<&probes::f_producer_well_formed>);

// ═══════════════════════════════════════════════════════════════════
// ── Runtime witnesses ────────────────────────────────────────────
// ═══════════════════════════════════════════════════════════════════

// 1. PipelineStage admission through alias — boolean readback at
//    runtime through a volatile sink so the optimizer cannot fold
//    the concept evaluation away entirely.
static void test_runtime_pipeline_stage_admission() {
    volatile bool ok_stage      = fw::is_pipeline_stage_v<&probes::f_stage_int_int>;
    volatile bool reject_stage  = fw::is_pipeline_stage_v<&probes::f_two_ints>;
    volatile bool reject_stage2 = fw::is_pipeline_stage_v<&probes::f_stage_2to1>;
    if (!ok_stage)      std::abort();
    if ( reject_stage)  std::abort();
    if ( reject_stage2) std::abort();
}

// 2. ConsumerEndpoint admission through alias.
static void test_runtime_consumer_endpoint_admission() {
    volatile bool ok_consumer    = fw::is_consumer_endpoint_v<&probes::f_consumer_well_formed>;
    volatile bool ok_consumer2   = fw::is_consumer_endpoint_v<&probes::f_consumer_value_mismatch>;
    volatile bool reject_consumer = fw::is_consumer_endpoint_v<&probes::f_two_ints>;
    if (!ok_consumer)     std::abort();
    if (!ok_consumer2)    std::abort();
    if ( reject_consumer) std::abort();
}

// 3. ProducerEndpoint admission through alias.
static void test_runtime_producer_endpoint_admission() {
    volatile bool ok_producer    = fw::is_producer_endpoint_v<&probes::f_producer_well_formed>;
    volatile bool ok_producer2   = fw::is_producer_endpoint_v<&probes::f_producer_value_mismatch>;
    volatile bool reject_producer = fw::is_producer_endpoint_v<&probes::f_two_ints>;
    if (!ok_producer)     std::abort();
    if (!ok_producer2)    std::abort();
    if ( reject_producer) std::abort();
}

// 4. Substrate smoke-test invocation through alias.  The substrate
//    headers ship inline smoke-test bodies; calling them through the
//    `extract` namespace exercises the substrate-side concept atom
//    while the umbrella's using-decl ensures the same admission set
//    is visible at `fw::` reach.
static void test_runtime_substrate_smoke_calls() {
    if (!extract::consumer_endpoint_smoke_test()) std::abort();
    if (!extract::producer_endpoint_smoke_test()) std::abort();
}

// 5. value_consistent_v readback — well-formed pair true; mismatch
//    pair false; on both consumer + producer sides.
static void test_runtime_value_consistent_predicate() {
    volatile bool consumer_ok  = fw::consumer_endpoint_value_consistent_v<&probes::f_consumer_well_formed>;
    volatile bool consumer_bad = fw::consumer_endpoint_value_consistent_v<&probes::f_consumer_value_mismatch>;
    volatile bool producer_ok  = fw::producer_endpoint_value_consistent_v<&probes::f_producer_well_formed>;
    volatile bool producer_bad = fw::producer_endpoint_value_consistent_v<&probes::f_producer_value_mismatch>;
    if (!consumer_ok)  std::abort();
    if ( consumer_bad) std::abort();
    if (!producer_ok)  std::abort();
    if ( producer_bad) std::abort();
}

// 6. value_preserving_v readback — int→int true; int→float false.
static void test_runtime_value_preserving_predicate() {
    volatile bool preserving = fw::pipeline_stage_is_value_preserving_v<&probes::f_stage_int_int>;
    volatile bool transforming = fw::pipeline_stage_is_value_preserving_v<&probes::f_stage_int_float>;
    if (!preserving)   std::abort();
    if ( transforming) std::abort();
}

// 7. VariadicPipelineStage admits non-1×1 stages; PipelineStage
//    does not.  Witness through the alias.
static void test_runtime_variadic_vs_strict_stage() {
    volatile bool variadic_admits = fw::VariadicPipelineStage<&probes::f_stage_2to1>;
    volatile bool strict_admits   = fw::PipelineStage<&probes::f_stage_2to1>;
    if (!variadic_admits) std::abort();
    if ( strict_admits)   std::abort();
}

// 8. Cross-shape mutual exclusion — a function cannot simultaneously
//    be PipelineStage AND ConsumerEndpoint AND ProducerEndpoint.
//    Readback through the alias.
static void test_runtime_cross_shape_exclusion() {
    // f_stage_int_int — PipelineStage only.
    {
        volatile bool s = fw::is_pipeline_stage_v<&probes::f_stage_int_int>;
        volatile bool c = fw::is_consumer_endpoint_v<&probes::f_stage_int_int>;
        volatile bool p = fw::is_producer_endpoint_v<&probes::f_stage_int_int>;
        if (!s) std::abort();
        if ( c) std::abort();
        if ( p) std::abort();
    }
    // f_consumer_well_formed — ConsumerEndpoint only.
    {
        volatile bool s = fw::is_pipeline_stage_v<&probes::f_consumer_well_formed>;
        volatile bool c = fw::is_consumer_endpoint_v<&probes::f_consumer_well_formed>;
        volatile bool p = fw::is_producer_endpoint_v<&probes::f_consumer_well_formed>;
        if ( s) std::abort();
        if (!c) std::abort();
        if ( p) std::abort();
    }
    // f_producer_well_formed — ProducerEndpoint only.
    {
        volatile bool s = fw::is_pipeline_stage_v<&probes::f_producer_well_formed>;
        volatile bool c = fw::is_consumer_endpoint_v<&probes::f_producer_well_formed>;
        volatile bool p = fw::is_producer_endpoint_v<&probes::f_producer_well_formed>;
        if ( s) std::abort();
        if ( c) std::abort();
        if (!p) std::abort();
    }
}

// ═══════════════════════════════════════════════════════════════════
// ── Driver ────────────────────────────────────────────────────────
// ═══════════════════════════════════════════════════════════════════

int main() {
    test_runtime_pipeline_stage_admission();
    test_runtime_consumer_endpoint_admission();
    test_runtime_producer_endpoint_admission();
    test_runtime_substrate_smoke_calls();
    test_runtime_value_consistent_predicate();
    test_runtime_value_preserving_predicate();
    test_runtime_variadic_vs_strict_stage();
    test_runtime_cross_shape_exclusion();
    std::printf("test_fixy_wrap_pipeline_stage_endpoint: "
                "8/8 runtime witnesses passed\n");
    return 0;
}
