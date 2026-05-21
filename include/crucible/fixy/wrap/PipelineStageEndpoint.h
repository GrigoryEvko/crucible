#pragma once

// ═══════════════════════════════════════════════════════════════════
// fixy::wrap::PipelineStageEndpoint — V-043 surface
//
// Re-exports the three dispatch-shape recognizer substrates that live
// in `crucible::safety::extract::`:
//
//   * crucible/safety/PipelineStage.h     — 1-in / 1-out body shape
//   * crucible/safety/ConsumerEndpoint.h  — consumer-handle × OwnedRegion
//                                           (drain to output buffer)
//   * crucible/safety/ProducerEndpoint.h  — producer-handle × OwnedRegion
//                                           (publish from input buffer)
//
// The trio co-evolves around `mint_stage_from_endpoints` (the Tier 2→3
// bridge in §XXI) — combining them into one umbrella mirrors V-041
// SimdWorkloadLocality.h precedent (Simd + Workload + LocalityHint).
//
// Substrate doc-blocks: see the per-substrate headers.  Each ships
// header-internal static_asserts that THIS file triggers under the
// project's warnings-as-errors flags
// (feedback_header_only_static_assert_blind_spot.md).
//
// ─── Public surface (20 symbols) ────────────────────────────────────
//
//   PipelineStage substrate (8):
//     VariadicPipelineStage<FnPtr>                 (concept)
//     PipelineStage<FnPtr>                         (concept)
//     is_pipeline_stage_v<FnPtr>                   (bool)
//     pipeline_stage_input_value_at_t<FnPtr, I>    (type alias)
//     pipeline_stage_output_value_at_t<FnPtr, I>   (type alias)
//     pipeline_stage_input_value_t<FnPtr>          (type alias, 1×1)
//     pipeline_stage_output_value_t<FnPtr>         (type alias, 1×1)
//     pipeline_stage_is_value_preserving_v<FnPtr>  (bool)
//
//   ConsumerEndpoint substrate (6):
//     ConsumerEndpoint<FnPtr>                      (concept)
//     is_consumer_endpoint_v<FnPtr>                (bool)
//     consumer_endpoint_handle_value_t<FnPtr>      (type alias)
//     consumer_endpoint_region_tag_t<FnPtr>        (type alias)
//     consumer_endpoint_region_value_t<FnPtr>      (type alias)
//     consumer_endpoint_value_consistent_v<FnPtr>  (bool)
//
//   ProducerEndpoint substrate (6):
//     ProducerEndpoint<FnPtr>                      (concept)
//     is_producer_endpoint_v<FnPtr>                (bool)
//     producer_endpoint_handle_value_t<FnPtr>      (type alias)
//     producer_endpoint_region_tag_t<FnPtr>        (type alias)
//     producer_endpoint_region_value_t<FnPtr>      (type alias)
//     producer_endpoint_value_consistent_v<FnPtr>  (bool)

#include <crucible/safety/ConsumerEndpoint.h>
#include <crucible/safety/OwnedRegion.h>
#include <crucible/safety/PipelineStage.h>
#include <crucible/safety/ProducerEndpoint.h>

#include <cstddef>
#include <optional>
#include <type_traits>

namespace crucible::fixy::wrap {

// ═══════════════════════════════════════════════════════════════════
// ── 1. PipelineStage — 1-in / 1-out body shape (8) ───────────────
// ═══════════════════════════════════════════════════════════════════

using ::crucible::safety::extract::VariadicPipelineStage;
using ::crucible::safety::extract::PipelineStage;
using ::crucible::safety::extract::is_pipeline_stage_v;
using ::crucible::safety::extract::pipeline_stage_input_value_at_t;
using ::crucible::safety::extract::pipeline_stage_output_value_at_t;
using ::crucible::safety::extract::pipeline_stage_input_value_t;
using ::crucible::safety::extract::pipeline_stage_output_value_t;
using ::crucible::safety::extract::pipeline_stage_is_value_preserving_v;

// ═══════════════════════════════════════════════════════════════════
// ── 2. ConsumerEndpoint — consumer-handle × OwnedRegion (6) ──────
// ═══════════════════════════════════════════════════════════════════

using ::crucible::safety::extract::ConsumerEndpoint;
using ::crucible::safety::extract::is_consumer_endpoint_v;
using ::crucible::safety::extract::consumer_endpoint_handle_value_t;
using ::crucible::safety::extract::consumer_endpoint_region_tag_t;
using ::crucible::safety::extract::consumer_endpoint_region_value_t;
using ::crucible::safety::extract::consumer_endpoint_value_consistent_v;

// ═══════════════════════════════════════════════════════════════════
// ── 3. ProducerEndpoint — producer-handle × OwnedRegion (6) ──────
// ═══════════════════════════════════════════════════════════════════

using ::crucible::safety::extract::ProducerEndpoint;
using ::crucible::safety::extract::is_producer_endpoint_v;
using ::crucible::safety::extract::producer_endpoint_handle_value_t;
using ::crucible::safety::extract::producer_endpoint_region_tag_t;
using ::crucible::safety::extract::producer_endpoint_region_value_t;
using ::crucible::safety::extract::producer_endpoint_value_consistent_v;

}  // namespace crucible::fixy::wrap

// ═══════════════════════════════════════════════════════════════════
// ── Dual-export sentinel — FIXY-V-043 ──────────────────────────────
// ═══════════════════════════════════════════════════════════════════
//
// Header-internal identity sentinels.  Same discipline as
// fixy/wrap/SimdWorkloadLocality.h (V-041), fixy/wrap/Checked.h
// (V-042).  Verifies each surface resolves to the substrate symbol
// with matching identity / value / concept admission.
//
// Synthetic probe types mirror the substrate's self-test scaffolding:
// minimal D04 consumer-handle (`try_pop`-only) and D05 producer-handle
// (`try_push`-only) over an integer payload + an OwnedRegion under
// a probe tag.

namespace crucible::fixy::wrap::self_test_pipeline_stage_endpoint {

// ── Synthetic probe types ────────────────────────────────────────

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

// ── Synthetic probe functions ────────────────────────────────────
//
// f_stage_int_int          — PipelineStage admit case (1×1, int → int)
// f_stage_int_float        — PipelineStage admit case (1×1, transform)
// f_consumer_endpoint_int  — ConsumerEndpoint admit case
// f_producer_endpoint_int  — ProducerEndpoint admit case
// f_consumer_endpoint_mismatch — ConsumerEndpoint admit (handle/region payload differ)

inline void f_stage_int_int(probe_consumer_handle<int>&&,
                            probe_producer_handle<int>&&) noexcept {}

inline void f_stage_int_float(probe_consumer_handle<int>&&,
                              probe_producer_handle<float>&&) noexcept {}

inline void f_consumer_endpoint_int(
    probe_consumer_handle<int>&&,
    ::crucible::safety::OwnedRegion<int, ProbeTagA>&&) noexcept {}

inline void f_consumer_endpoint_mismatch(
    probe_consumer_handle<int>&&,
    ::crucible::safety::OwnedRegion<double, ProbeTagA>&&) noexcept {}

inline void f_producer_endpoint_int(
    probe_producer_handle<int>&&,
    ::crucible::safety::OwnedRegion<int, ProbeTagB>&&) noexcept {}

inline void f_producer_endpoint_mismatch(
    probe_producer_handle<int>&&,
    ::crucible::safety::OwnedRegion<double, ProbeTagB>&&) noexcept {}

// Negative-case probe — neither stage nor endpoint shape.
inline void f_two_ints(int, int) noexcept {}

// ── 1. PipelineStage concept admission identity ───────────────────
//
// Both reach paths admit / reject the same FnPtrs.  Cross-path
// equality is a substantive identity witness because each concept
// instantiation is a separate compile-time atomic-constraint
// evaluation; if the using-decl elided the concept atom, the two
// would diverge under a TU that inadvertently shadowed it.

static_assert( ::crucible::fixy::wrap::PipelineStage<&f_stage_int_int>);
static_assert( ::crucible::fixy::wrap::PipelineStage<&f_stage_int_float>);
static_assert(!::crucible::fixy::wrap::PipelineStage<&f_two_ints>);
static_assert(!::crucible::fixy::wrap::PipelineStage<&f_consumer_endpoint_int>);
static_assert(!::crucible::fixy::wrap::PipelineStage<&f_producer_endpoint_int>);

// Variadic admission — a 1×1 stage is also variadic; a non-stage is not.
static_assert( ::crucible::fixy::wrap::VariadicPipelineStage<&f_stage_int_int>);
static_assert(!::crucible::fixy::wrap::VariadicPipelineStage<&f_two_ints>);

// Cross-path agreement on admission.
static_assert(
    ::crucible::fixy::wrap::PipelineStage<&f_stage_int_int> ==
    ::crucible::safety::extract::PipelineStage<&f_stage_int_int>);
static_assert(
    ::crucible::fixy::wrap::PipelineStage<&f_two_ints> ==
    ::crucible::safety::extract::PipelineStage<&f_two_ints>);
static_assert(
    ::crucible::fixy::wrap::VariadicPipelineStage<&f_stage_int_int> ==
    ::crucible::safety::extract::VariadicPipelineStage<&f_stage_int_int>);

// ── 2. PipelineStage is_pipeline_stage_v cross-path identity ──────

static_assert(
    ::crucible::fixy::wrap::is_pipeline_stage_v<&f_stage_int_int> ==
    ::crucible::safety::extract::is_pipeline_stage_v<&f_stage_int_int>);
static_assert(
    ::crucible::fixy::wrap::is_pipeline_stage_v<&f_stage_int_int> == true);
static_assert(
    ::crucible::fixy::wrap::is_pipeline_stage_v<&f_two_ints> == false);

// ── 3. PipelineStage extractor type-alias identity ────────────────
//
// `pipeline_stage_input_value_t<&f>` produces a type per FnPtr.
// is_same_v witness across reach paths + against the expected type.

static_assert(std::is_same_v<
    ::crucible::fixy::wrap::pipeline_stage_input_value_t<&f_stage_int_int>,
    ::crucible::safety::extract::pipeline_stage_input_value_t<&f_stage_int_int>>);
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::pipeline_stage_input_value_t<&f_stage_int_int>, int>);
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::pipeline_stage_output_value_t<&f_stage_int_int>, int>);
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::pipeline_stage_output_value_t<&f_stage_int_float>, float>);

// _at_t variants — index 0 of a 1×1 stage matches the legacy alias.
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::pipeline_stage_input_value_at_t<&f_stage_int_int, 0>,
    ::crucible::safety::extract::pipeline_stage_input_value_at_t<&f_stage_int_int, 0>>);
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::pipeline_stage_input_value_at_t<&f_stage_int_int, 0>, int>);
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::pipeline_stage_output_value_at_t<&f_stage_int_float, 0>, float>);

// ── 4. PipelineStage is_value_preserving_v cross-path identity ────

static_assert(
    ::crucible::fixy::wrap::pipeline_stage_is_value_preserving_v<&f_stage_int_int> ==
    ::crucible::safety::extract::pipeline_stage_is_value_preserving_v<&f_stage_int_int>);
static_assert(
    ::crucible::fixy::wrap::pipeline_stage_is_value_preserving_v<&f_stage_int_int> == true);
static_assert(
    ::crucible::fixy::wrap::pipeline_stage_is_value_preserving_v<&f_stage_int_float> == false);

// ── 5. ConsumerEndpoint concept admission identity ────────────────

static_assert( ::crucible::fixy::wrap::ConsumerEndpoint<&f_consumer_endpoint_int>);
static_assert( ::crucible::fixy::wrap::ConsumerEndpoint<&f_consumer_endpoint_mismatch>);
static_assert(!::crucible::fixy::wrap::ConsumerEndpoint<&f_two_ints>);
static_assert(!::crucible::fixy::wrap::ConsumerEndpoint<&f_stage_int_int>);
static_assert(!::crucible::fixy::wrap::ConsumerEndpoint<&f_producer_endpoint_int>);

// Cross-path agreement.
static_assert(
    ::crucible::fixy::wrap::ConsumerEndpoint<&f_consumer_endpoint_int> ==
    ::crucible::safety::extract::ConsumerEndpoint<&f_consumer_endpoint_int>);
static_assert(
    ::crucible::fixy::wrap::ConsumerEndpoint<&f_two_ints> ==
    ::crucible::safety::extract::ConsumerEndpoint<&f_two_ints>);

// ── 6. ConsumerEndpoint is_consumer_endpoint_v cross-path identity ─

static_assert(
    ::crucible::fixy::wrap::is_consumer_endpoint_v<&f_consumer_endpoint_int> ==
    ::crucible::safety::extract::is_consumer_endpoint_v<&f_consumer_endpoint_int>);
static_assert(
    ::crucible::fixy::wrap::is_consumer_endpoint_v<&f_consumer_endpoint_int> == true);
static_assert(
    ::crucible::fixy::wrap::is_consumer_endpoint_v<&f_two_ints> == false);

// ── 7. ConsumerEndpoint extractor type-alias identity ─────────────

static_assert(std::is_same_v<
    ::crucible::fixy::wrap::consumer_endpoint_handle_value_t<&f_consumer_endpoint_int>,
    ::crucible::safety::extract::consumer_endpoint_handle_value_t<&f_consumer_endpoint_int>>);
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::consumer_endpoint_handle_value_t<&f_consumer_endpoint_int>, int>);
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::consumer_endpoint_region_tag_t<&f_consumer_endpoint_int>,
    ProbeTagA>);
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::consumer_endpoint_region_value_t<&f_consumer_endpoint_int>, int>);
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::consumer_endpoint_region_value_t<&f_consumer_endpoint_mismatch>, double>);

// ── 8. ConsumerEndpoint value_consistent_v cross-path identity ────
//
// Predicate is true when handle's payload == region's element type.
// f_consumer_endpoint_int passes; f_consumer_endpoint_mismatch fails.

static_assert(
    ::crucible::fixy::wrap::consumer_endpoint_value_consistent_v<&f_consumer_endpoint_int> ==
    ::crucible::safety::extract::consumer_endpoint_value_consistent_v<&f_consumer_endpoint_int>);
static_assert(
    ::crucible::fixy::wrap::consumer_endpoint_value_consistent_v<&f_consumer_endpoint_int> == true);
static_assert(
    ::crucible::fixy::wrap::consumer_endpoint_value_consistent_v<&f_consumer_endpoint_mismatch> == false);

// ── 9. ProducerEndpoint concept admission identity ────────────────

static_assert( ::crucible::fixy::wrap::ProducerEndpoint<&f_producer_endpoint_int>);
static_assert( ::crucible::fixy::wrap::ProducerEndpoint<&f_producer_endpoint_mismatch>);
static_assert(!::crucible::fixy::wrap::ProducerEndpoint<&f_two_ints>);
static_assert(!::crucible::fixy::wrap::ProducerEndpoint<&f_stage_int_int>);
static_assert(!::crucible::fixy::wrap::ProducerEndpoint<&f_consumer_endpoint_int>);

// Cross-path agreement.
static_assert(
    ::crucible::fixy::wrap::ProducerEndpoint<&f_producer_endpoint_int> ==
    ::crucible::safety::extract::ProducerEndpoint<&f_producer_endpoint_int>);
static_assert(
    ::crucible::fixy::wrap::ProducerEndpoint<&f_two_ints> ==
    ::crucible::safety::extract::ProducerEndpoint<&f_two_ints>);

// ── 10. ProducerEndpoint is_producer_endpoint_v cross-path identity ─

static_assert(
    ::crucible::fixy::wrap::is_producer_endpoint_v<&f_producer_endpoint_int> ==
    ::crucible::safety::extract::is_producer_endpoint_v<&f_producer_endpoint_int>);
static_assert(
    ::crucible::fixy::wrap::is_producer_endpoint_v<&f_producer_endpoint_int> == true);
static_assert(
    ::crucible::fixy::wrap::is_producer_endpoint_v<&f_two_ints> == false);

// ── 11. ProducerEndpoint extractor type-alias identity ────────────

static_assert(std::is_same_v<
    ::crucible::fixy::wrap::producer_endpoint_handle_value_t<&f_producer_endpoint_int>,
    ::crucible::safety::extract::producer_endpoint_handle_value_t<&f_producer_endpoint_int>>);
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::producer_endpoint_handle_value_t<&f_producer_endpoint_int>, int>);
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::producer_endpoint_region_tag_t<&f_producer_endpoint_int>,
    ProbeTagB>);
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::producer_endpoint_region_value_t<&f_producer_endpoint_int>, int>);
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::producer_endpoint_region_value_t<&f_producer_endpoint_mismatch>, double>);

// ── 12. ProducerEndpoint value_consistent_v cross-path identity ───

static_assert(
    ::crucible::fixy::wrap::producer_endpoint_value_consistent_v<&f_producer_endpoint_int> ==
    ::crucible::safety::extract::producer_endpoint_value_consistent_v<&f_producer_endpoint_int>);
static_assert(
    ::crucible::fixy::wrap::producer_endpoint_value_consistent_v<&f_producer_endpoint_int> == true);
static_assert(
    ::crucible::fixy::wrap::producer_endpoint_value_consistent_v<&f_producer_endpoint_mismatch> == false);

// ── 13. Cross-shape exclusion ─────────────────────────────────────
//
// PipelineStage / ConsumerEndpoint / ProducerEndpoint are mutually
// exclusive on the shape matchers.  A function that is one cannot
// be any other.

static_assert( ::crucible::fixy::wrap::PipelineStage<&f_stage_int_int>);
static_assert(!::crucible::fixy::wrap::ConsumerEndpoint<&f_stage_int_int>);
static_assert(!::crucible::fixy::wrap::ProducerEndpoint<&f_stage_int_int>);

static_assert(!::crucible::fixy::wrap::PipelineStage<&f_consumer_endpoint_int>);
static_assert( ::crucible::fixy::wrap::ConsumerEndpoint<&f_consumer_endpoint_int>);
static_assert(!::crucible::fixy::wrap::ProducerEndpoint<&f_consumer_endpoint_int>);

static_assert(!::crucible::fixy::wrap::PipelineStage<&f_producer_endpoint_int>);
static_assert(!::crucible::fixy::wrap::ConsumerEndpoint<&f_producer_endpoint_int>);
static_assert( ::crucible::fixy::wrap::ProducerEndpoint<&f_producer_endpoint_int>);

// ── Cardinality witness ──────────────────────────────────────────
//
// 20 surfaced using-declarations across 3 substrates:
//
//   PipelineStage     (8) — VariadicPipelineStage / PipelineStage
//                            concepts + is_pipeline_stage_v +
//                            {input,output}_value_{at_t,_t} +
//                            is_value_preserving_v
//   ConsumerEndpoint  (6) — ConsumerEndpoint concept + is_v +
//                            {handle,region}_{value,tag}_t +
//                            value_consistent_v
//   ProducerEndpoint  (6) — ProducerEndpoint concept + is_v +
//                            {handle,region}_{value,tag}_t +
//                            value_consistent_v
//
// Future additions to any of the three substrates MUST extend this
// block + bump the constant + add a sentinel above.

constexpr int pipeline_stage_endpoint_alias_cardinality = 20;
static_assert(pipeline_stage_endpoint_alias_cardinality == 20,
    "fixy::wrap::PipelineStageEndpoint cardinality changed — update "
    "PipelineStageEndpoint.h sentinel block to track the three "
    "dispatch-shape recognizer substrates' public surface.");

}  // namespace crucible::fixy::wrap::self_test_pipeline_stage_endpoint
