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
//   StageArity<auto FnPtr>
//                          Metafunction exposing input_count,
//                          output_count, and ordered for variadic
//                          pipeline bodies.  Inputs are the leading
//                          consumer-handle parameters; outputs are
//                          the trailing producer-handle parameters.
//
//   VariadicPipelineStage<auto FnPtr>
//                          Concept satisfied iff FnPtr is a void
//                          function whose parameters are one-or-more
//                          consumer handles followed by one-or-more
//                          producer handles, all non-const rvalue
//                          references.  This is the GAPS-085 fan-in /
//                          fan-out concept; the runtime mint factory
//                          remains the 1x1 `mint_stage` until
//                          GAPS-086.
//
//   PipelineStage<auto FnPtr>
//                          Concept satisfied iff FnPtr's signature
//                          matches the legacy 1-input / 1-output
//                          pipeline-stage shape:
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

#include <cstddef>
#include <optional>
#include <type_traits>
#include <utility>

namespace crucible::safety::extract {

// ═════════════════════════════════════════════════════════════════════
// ── StageArity / VariadicPipelineStage / PipelineStage ─────────────
// ═════════════════════════════════════════════════════════════════════

namespace detail {

template <auto FnPtr, std::size_t I>
inline constexpr bool stage_param_rvalue_nonconst_v =
    std::is_rvalue_reference_v<param_type_t<FnPtr, I>>
 && !std::is_const_v<std::remove_reference_t<param_type_t<FnPtr, I>>>;

template <auto FnPtr, std::size_t I>
inline constexpr bool stage_input_param_v =
    stage_param_rvalue_nonconst_v<FnPtr, I>
 && is_consumer_handle_v<param_type_t<FnPtr, I>>;

template <auto FnPtr, std::size_t I>
inline constexpr bool stage_output_param_v =
    stage_param_rvalue_nonconst_v<FnPtr, I>
 && is_producer_handle_v<param_type_t<FnPtr, I>>;

struct stage_arity_counts {
    std::size_t input_count = 0;
    std::size_t output_count = 0;
    bool ordered = true;
};

template <auto FnPtr, std::size_t I>
consteval void consume_stage_param(stage_arity_counts& counts,
                                   bool& seen_output) noexcept {
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
consteval stage_arity_counts
compute_stage_arity(std::index_sequence<Is...>) noexcept {
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
        detail::compute_stage_arity<FnPtr>(
            std::make_index_sequence<arity_v<FnPtr>>{});

public:
    static constexpr std::size_t input_count = counts.input_count;
    static constexpr std::size_t output_count = counts.output_count;
    static constexpr bool ordered = counts.ordered;
};

template <auto FnPtr>
concept VariadicPipelineStage =
    arity_v<FnPtr> >= 2
 && std::is_void_v<return_type_t<FnPtr>>
 && StageArity<FnPtr>::ordered
 && StageArity<FnPtr>::input_count > 0
 && StageArity<FnPtr>::output_count > 0
 && StageArity<FnPtr>::input_count + StageArity<FnPtr>::output_count
        == arity_v<FnPtr>;

template <auto FnPtr>
concept PipelineStage =
    VariadicPipelineStage<FnPtr>
 && StageArity<FnPtr>::input_count == 1
 && StageArity<FnPtr>::output_count == 1;

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

inline void f_one_to_one(fake_consumer<int>&&,
                         fake_producer<int>&&) noexcept {}
static_assert(VariadicPipelineStage<&f_one_to_one>);
static_assert(PipelineStage<&f_one_to_one>);
static_assert(StageArity<&f_one_to_one>::input_count == 1);
static_assert(StageArity<&f_one_to_one>::output_count == 1);

inline void f_three_to_one(fake_consumer<int>&&,
                           fake_consumer<int>&&,
                           fake_consumer<float>&&,
                           fake_producer<int>&&) noexcept {}
static_assert(VariadicPipelineStage<&f_three_to_one>);
static_assert(!PipelineStage<&f_three_to_one>);
static_assert(StageArity<&f_three_to_one>::input_count == 3);
static_assert(StageArity<&f_three_to_one>::output_count == 1);

inline void f_one_to_two(fake_consumer<int>&&,
                         fake_producer<int>&&,
                         fake_producer<float>&&) noexcept {}
static_assert(VariadicPipelineStage<&f_one_to_two>);
static_assert(!PipelineStage<&f_one_to_two>);
static_assert(StageArity<&f_one_to_two>::input_count == 1);
static_assert(StageArity<&f_one_to_two>::output_count == 2);

inline void f_interleaved(fake_consumer<int>&&,
                          fake_producer<int>&&,
                          fake_consumer<int>&&) noexcept {}
static_assert(!VariadicPipelineStage<&f_interleaved>);
static_assert(!PipelineStage<&f_interleaved>);

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
