#pragma once

// ── crucible::safety::extract::PipelineStage ────────────────────────
//
// FOUND-D19 of 27_04_2026.md §3.7 + §5.6 + 28_04_2026_effects.md §6.1.
// The pipeline-stage shape — a function holding BOTH a consumer
// handle (for the previous stage's output) AND a producer handle
// (for the next stage's input).  Closes the canonical-shape
// taxonomy alongside D15-D18.
//
// ── What this header ships ──────────────────────────────────────────
//
//   PipelineStage<auto FnPtr>
//                          Concept satisfied iff FnPtr's signature
//                          matches the pipeline-stage shape:
//                          - arity == 2
//                          - parameter 0 is a non-const rvalue
//                            reference to a consumer handle (D06)
//                          - parameter 1 is a non-const rvalue
//                            reference to a producer handle (D05)
//                          - return type is void
//
//   is_pipeline_stage_v<auto FnPtr>
//                          Variable-template form for use inside
//                          metaprogram folds.
//
//   pipeline_stage_input_value_t<auto FnPtr>
//                          The payload type the consumer handle's
//                          try_pop yields (the "In" type per §3.7).
//
//   pipeline_stage_output_value_t<auto FnPtr>
//                          The payload type the producer handle's
//                          try_push accepts (the "Out" type per §3.7).
//                          May or may not equal input_value_t — a
//                          stage that transforms input-typed
//                          messages into output-typed messages is
//                          valid.
//
//   pipeline_stage_is_value_preserving_v<auto FnPtr>
//                          Predicate: true iff input_value_t ==
//                          output_value_t (after cv-ref strip).
//                          Used by the dispatcher to choose between
//                          a forward-the-message-through fast path
//                          and a transformation lowering.
//
// ── Lowering target ─────────────────────────────────────────────────
//
// 27_04 §3.7: the dispatcher routes PipelineStage to
// `safety::parallel_pipeline<Stages...>` (THREADING.md §5.3) — each
// stage runs in its own jthread; inter-stage queues materialised at
// framework level; permission tree auto-generated as a chain.
//
// Concrete examples that match:
//
//   void stage_pass_through(ConsumerSyn&&, ProducerSyn&&) noexcept;
//   void stage_transform(ConsumerInt&&, ProducerFloat&&) noexcept;
//
// Examples that do NOT match:
//
//   void f(ConsumerHandle&&, OwnedRegion<T,X>&&);   // ConsumerEndpoint shape
//   void f(ProducerHandle&&, ConsumerHandle&&);     // wrong slot order
//   void f(ConsumerHandle&&);                       // arity 1
//   int  f(ConsumerHandle&&, ProducerHandle&&);     // non-void return
//   void f(ConsumerHandle&, ProducerHandle&&);      // consumer by lvalue ref
//   void f(ConsumerHandle&&, ProducerHandle&);      // producer by lvalue ref
//   void f(ConsumerHandle&&, SwmrWriter&&);         // SWMR writer in slot 1
//   void f(SwmrReader&&, ProducerHandle&&);         // SWMR reader in slot 0
//
// ── Pattern ─────────────────────────────────────────────────────────
//
// Five-clause concept conjunction.  Combines D05 IsProducerHandle on
// param 1 with D06 IsConsumerHandle on param 0 — the slot ORDER
// matters: consumer-side first (drain prev stage), producer-side
// second (push to next stage).  This mirrors the pipeline data-flow
// direction.
//
// ── Mutual exclusivity ──────────────────────────────────────────────
//
// PipelineStage must NOT collide with:
//   - ConsumerEndpoint — that has OwnedRegion in slot 1, not a
//     ProducerHandle.  D06 (consumer) vs D03 (region) — disjoint.
//   - ProducerEndpoint — slot 0 is a producer, not a consumer.  D05
//     and D06 are mutually exclusive (a handle has try_push xor
//     try_pop, never both).
//   - SwmrWriter / SwmrReader — those have non-handle param 1 (T
//     value) or arity 1 with non-void return; structurally disjoint.
//   - UnaryTransform / BinaryTransform — those require OwnedRegion
//     on slot 0; D06 (consumer-handle) on slot 0 fails OwnedRegion's
//     tag-template specialization.
//
// The exclusion proofs are pinned by the sentinel TU.
//
// ── Axiom coverage ──────────────────────────────────────────────────
//
//   InitSafe / NullSafe / MemSafe / BorrowSafe / ThreadSafe / LeakSafe
//     — N/A; pure consteval shape recognition.
//   TypeSafe — five orthogonal clauses; non-matching signatures
//              fail at the requires-clause boundary.
//   DetSafe — same FnPtr → same recognition result.

#include <crucible/safety/IsConsumerHandle.h>
#include <crucible/safety/IsProducerHandle.h>
#include <crucible/safety/SignatureTraits.h>

#include <type_traits>

namespace crucible::safety::extract {

// ═════════════════════════════════════════════════════════════════════
// ── PipelineStage concept ──────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

template <auto FnPtr>
concept PipelineStage =
    arity_v<FnPtr> == 2
    // Parameter 0: non-const rvalue reference to a consumer handle.
    // The slot order is fixed per §3.7 — consumer-side (drain prev
    // stage) first, producer-side (push to next stage) second.
    && std::is_rvalue_reference_v<param_type_t<FnPtr, 0>>
    && !std::is_const_v<std::remove_reference_t<param_type_t<FnPtr, 0>>>
    && is_consumer_handle_v<param_type_t<FnPtr, 0>>
    // Parameter 1: non-const rvalue reference to a producer handle.
    && std::is_rvalue_reference_v<param_type_t<FnPtr, 1>>
    && !std::is_const_v<std::remove_reference_t<param_type_t<FnPtr, 1>>>
    && is_producer_handle_v<param_type_t<FnPtr, 1>>
    // Return type — void per §3.7.
    && std::is_void_v<return_type_t<FnPtr>>;

template <auto FnPtr>
inline constexpr bool is_pipeline_stage_v = PipelineStage<FnPtr>;

// ═════════════════════════════════════════════════════════════════════
// ── Extractors ─────────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

// Input-side payload type (consumer handle's try_pop yields this).
template <auto FnPtr>
    requires PipelineStage<FnPtr>
using pipeline_stage_input_value_t =
    consumer_handle_value_t<param_type_t<FnPtr, 0>>;

// Output-side payload type (producer handle's try_push accepts this).
template <auto FnPtr>
    requires PipelineStage<FnPtr>
using pipeline_stage_output_value_t =
    producer_handle_value_t<param_type_t<FnPtr, 1>>;

// ═════════════════════════════════════════════════════════════════════
// ── Value-preservation predicate ───────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// True iff input_value_t == output_value_t — the stage forwards
// messages of the same type from prev to next.  False means the
// stage transforms (e.g., int → float).  Both are valid pipeline
// stages; the dispatcher uses this predicate to choose between a
// pass-through fast path (memcpy / forward-the-bytes) and a
// transform lowering (call-the-stage-body, marshal output).

template <auto FnPtr>
    requires PipelineStage<FnPtr>
inline constexpr bool pipeline_stage_is_value_preserving_v =
    std::is_same_v<pipeline_stage_input_value_t<FnPtr>,
                   pipeline_stage_output_value_t<FnPtr>>;

// ═════════════════════════════════════════════════════════════════════
// ── Self-test block ────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace detail::pipeline_stage_self_test {

inline void f_nullary() noexcept {}
static_assert(!PipelineStage<&f_nullary>);

inline void f_one_int(int) noexcept {}
static_assert(!PipelineStage<&f_one_int>);

inline void f_two_ints(int, int) noexcept {}
static_assert(!PipelineStage<&f_two_ints>);

inline void f_three_params(int, int, int) noexcept {}
static_assert(!PipelineStage<&f_three_params>);

}  // namespace detail::pipeline_stage_self_test

// ═════════════════════════════════════════════════════════════════════
// ── Runtime smoke test ─────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

inline bool pipeline_stage_smoke_test() noexcept {
    using namespace detail::pipeline_stage_self_test;

    volatile std::size_t const cap = 4;
    bool ok = true;
    for (std::size_t i = 0; i < cap; ++i) {
        ok = ok && !PipelineStage<&f_nullary>;
        ok = ok && !PipelineStage<&f_one_int>;
        ok = ok && !PipelineStage<&f_two_ints>;
        ok = ok && !PipelineStage<&f_three_params>;
    }
    return ok;
}

}  // namespace crucible::safety::extract
