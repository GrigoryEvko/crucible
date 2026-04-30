// ═══════════════════════════════════════════════════════════════════
// test_pipeline_stage — sentinel TU for safety/PipelineStage.h
//
// Per 27_04_2026.md §3.7:
//
//   void stage(ConsumerHandle<In, K1, S1>&&,
//              ProducerHandle<Out, K2, S2>&&)
//
// Slot order is fixed: consumer first (drain), producer second
// (push).  The framework runs each stage in its own jthread with
// inter-stage queues materialised at framework level.
//
// Coverage:
//   * Positive: well-formed pass-through (consumer<int> →
//     producer<int>) — value-preserving.
//   * Positive: transform stage (consumer<int> → producer<float>)
//     — concept admits, predicate distinguishes.
//   * Positive: non-noexcept handles (D05/D06 second-decomp branch).
//   * Negative: arity 0, 1, 3.
//   * Negative: consumer by lvalue ref / const&&.
//   * Negative: producer by lvalue ref / const&&.
//   * Negative: slot order swapped (producer in slot 0).
//   * Negative: SwmrWriter / SwmrReader in either slot.
//   * Negative: hybrid handle in either slot (D05/D06 reject).
//   * Negative: OwnedRegion in either slot (overlaps Consumer/
//     ProducerEndpoint shapes).
//   * Negative: non-handle in either slot.
//   * Negative: non-void return.
//   * Cross-shape exclusion vs every other canonical shape.
//   * D11 inferred_permission_tags_t: empty set (handles are not
//     OwnedRegion-shaped).
//   * Volatile&& on either slot admitted.
// ═══════════════════════════════════════════════════════════════════

#include <crucible/safety/PipelineStage.h>

#include <crucible/safety/BinaryTransform.h>
#include <crucible/safety/ConsumerEndpoint.h>
#include <crucible/safety/InferredPermissionTags.h>
#include <crucible/safety/OwnedRegion.h>
#include <crucible/safety/ProducerEndpoint.h>
#include <crucible/safety/SwmrReader.h>
#include <crucible/safety/SwmrWriter.h>
#include <crucible/safety/UnaryTransform.h>

#include <cstdio>
#include <cstdlib>
#include <optional>
#include <type_traits>

namespace {

struct TestFailure {};
int total_passed = 0;
int total_failed = 0;

template <typename F>
void run_test(const char* name, F&& body) {
    std::fprintf(stderr, "  %s: ", name);
    try {
        body();
        ++total_passed;
        std::fprintf(stderr, "PASSED\n");
    } catch (TestFailure&) {
        ++total_failed;
        std::fprintf(stderr, "FAILED\n");
    }
}

#define EXPECT_TRUE(cond)                                                  \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::fprintf(stderr,                                           \
                "    EXPECT_TRUE failed: %s (%s:%d)\n",                    \
                #cond, __FILE__, __LINE__);                                \
            throw TestFailure{};                                           \
        }                                                                  \
    } while (0)

namespace extract = ::crucible::safety::extract;
namespace safety  = ::crucible::safety;

struct in_tag  {};

}  // namespace

namespace ps_test {

// Consumer-handle witnesses — match D06 (try_pop returning
// optional<P>, no try_push).
struct consumer_int {
    [[nodiscard]] std::optional<int> try_pop() noexcept { return 0; }
};
struct consumer_float {
    [[nodiscard]] std::optional<float> try_pop() noexcept { return 0.0f; }
};
struct consumer_int_no_noexcept {
    [[nodiscard]] std::optional<int> try_pop() { return 0; }
};

// Producer-handle witnesses — match D05 (try_push(P const&)
// returning bool, no try_pop).
struct producer_int {
    [[nodiscard]] bool try_push(int const&) noexcept { return true; }
};
struct producer_float {
    [[nodiscard]] bool try_push(float const&) noexcept { return true; }
};
struct producer_int_no_noexcept {
    [[nodiscard]] bool try_push(int const&) { return true; }
};

// Hybrid — both try_push AND try_pop.  D05 and D06 BOTH reject.
struct hybrid_handle {
    [[nodiscard]] bool try_push(int const&) noexcept { return true; }
    [[nodiscard]] std::optional<int> try_pop() noexcept { return 0; }
};

// SWMR handles for cross-shape exclusion tests.
struct swmr_writer {
    void publish(int const&) noexcept {}
};
struct swmr_reader {
    int load() const noexcept { return 0; }
};

// Pointer-payload handles — D05/D06 admit because P deduces to int*.
struct consumer_ptr {
    [[nodiscard]] std::optional<int*> try_pop() noexcept { return nullptr; }
};
struct producer_ptr {
    [[nodiscard]] bool try_push(int* const&) noexcept { return true; }
};

// Handles with extra unrelated methods — D05/D06 detect publish/pop
// only; extras are structurally invisible.
struct consumer_with_extras {
    [[nodiscard]] std::optional<int> try_pop() noexcept { return 0; }
    void unrelated_helper() noexcept {}
    int  another_method(int) const noexcept { return 0; }
};
struct producer_with_extras {
    [[nodiscard]] bool try_push(int const&) noexcept { return true; }
    void unrelated_helper() noexcept {}
};

// cv-qualified consumer method (try_pop is const) — D06 rejects
// because load_signature_decomp matches mptr types WITHOUT cv-ref
// qualifiers on the method.
struct consumer_const_pop {
    [[nodiscard]] std::optional<int> try_pop() const noexcept { return 0; }
};

// cv-qualified producer method (try_push is const) — D05 rejects.
struct producer_const_push {
    [[nodiscard]] bool try_push(int const&) const noexcept { return true; }
};

using OR_int = ::crucible::safety::OwnedRegion<int, ::in_tag>;

// ── Positive shapes ─────────────────────────────────────────────

// Pass-through stage: same type in/out.
void f_pass_through(consumer_int&&, producer_int&&) noexcept;

// Transform stage: int → float.
void f_transform(consumer_int&&, producer_float&&) noexcept;

// Float → int (reverse direction transform).
void f_transform_reverse(consumer_float&&, producer_int&&) noexcept;

// Non-noexcept consumer handle.
void f_consumer_no_noexcept(consumer_int_no_noexcept&&,
                            producer_int&&) noexcept;

// Non-noexcept producer handle.
void f_producer_no_noexcept(consumer_int&&,
                            producer_int_no_noexcept&&) noexcept;

// Volatile&& on either slot — admitted (orthogonal axis).
void f_consumer_volatile(consumer_int volatile&&,
                         producer_int&&) noexcept;
void f_producer_volatile(consumer_int&&,
                         producer_int volatile&&) noexcept;

// Volatile&& on BOTH slots — also admitted.
void f_both_volatile(consumer_int volatile&&,
                     producer_int volatile&&) noexcept;

// Pointer-payload pipeline stage — admitted; pass-through.
void f_pointer_pass_through(consumer_ptr&&, producer_ptr&&) noexcept;

// Stage with extras-bearing handles — admitted; structural detection.
void f_extras_handles(consumer_with_extras&&,
                      producer_with_extras&&) noexcept;

// Both slots non-noexcept — admitted; both decomp branches active.
void f_both_no_noexcept(consumer_int_no_noexcept&&,
                        producer_int_no_noexcept&&) noexcept;

// ── Negative shapes ─────────────────────────────────────────────

// Arity wrong.
void f_no_param() noexcept;
void f_one_consumer(consumer_int&&) noexcept;
void f_three_params(consumer_int&&, producer_int&&, int) noexcept;

// Consumer by lvalue ref / const&&.
void f_consumer_lvalue_ref(consumer_int&, producer_int&&) noexcept;
void f_consumer_const_rvalue_ref(consumer_int const&&,
                                 producer_int&&) noexcept;

// Producer by lvalue ref / const&&.
void f_producer_lvalue_ref(consumer_int&&, producer_int&) noexcept;
void f_producer_const_rvalue_ref(consumer_int&&,
                                 producer_int const&&) noexcept;

// Slot order swapped — producer in slot 0, consumer in slot 1.
// Should be rejected because IsConsumerHandle on slot 0 fails.
void f_swapped_slot_order(producer_int&&, consumer_int&&) noexcept;

// Hybrid in either slot — D05/D06 reject.
void f_hybrid_consumer_slot(hybrid_handle&&, producer_int&&) noexcept;
void f_hybrid_producer_slot(consumer_int&&, hybrid_handle&&) noexcept;

// SWMR writer in producer slot — has publish, not try_push.
void f_swmr_writer_in_producer_slot(consumer_int&&,
                                    swmr_writer&&) noexcept;

// SWMR reader in consumer slot — has load, not try_pop.
void f_swmr_reader_in_consumer_slot(swmr_reader&&,
                                    producer_int&&) noexcept;

// OwnedRegion in either slot — explicitly rejected because
// OwnedRegion is not handle-shaped (D05/D06 reject), but worth
// pinning the rejection.
void f_region_in_consumer_slot(OR_int&&, producer_int&&) noexcept;
void f_region_in_producer_slot(consumer_int&&, OR_int&&) noexcept;

// Non-handle in either slot.
void f_int_in_consumer_slot(int, producer_int&&) noexcept;
void f_int_in_producer_slot(consumer_int&&, int) noexcept;

// Non-void return.
int f_int_return(consumer_int&&, producer_int&&) noexcept;

// cv-qualified handle method on consumer slot — D06 rejects.
void f_const_pop_in_consumer_slot(consumer_const_pop&&,
                                  producer_int&&) noexcept;

// cv-qualified handle method on producer slot — D05 rejects.
void f_const_push_in_producer_slot(consumer_int&&,
                                   producer_const_push&&) noexcept;

}  // namespace ps_test

namespace {

// ── Tests ────────────────────────────────────────────────────────

void test_runtime_smoke() {
    EXPECT_TRUE(extract::pipeline_stage_smoke_test());
}

void test_positive_pass_through() {
    static_assert( extract::PipelineStage<&ps_test::f_pass_through>);
    static_assert( extract::is_pipeline_stage_v<
        &ps_test::f_pass_through>);
    static_assert( extract::pipeline_stage_is_value_preserving_v<
        &ps_test::f_pass_through>);
}

void test_positive_transform() {
    // int → float — concept admits, but value_preserving is false.
    static_assert( extract::PipelineStage<&ps_test::f_transform>);
    static_assert(!extract::pipeline_stage_is_value_preserving_v<
        &ps_test::f_transform>);
}

void test_positive_transform_reverse() {
    // Direction is independent of types — float → int is also a
    // valid transform stage.
    static_assert( extract::PipelineStage<&ps_test::f_transform_reverse>);
    static_assert(!extract::pipeline_stage_is_value_preserving_v<
        &ps_test::f_transform_reverse>);
}

void test_positive_non_noexcept_handles() {
    static_assert( extract::PipelineStage<
        &ps_test::f_consumer_no_noexcept>);
    static_assert( extract::PipelineStage<
        &ps_test::f_producer_no_noexcept>);
}

void test_positive_volatile_either_slot() {
    static_assert( extract::PipelineStage<&ps_test::f_consumer_volatile>);
    static_assert( extract::PipelineStage<&ps_test::f_producer_volatile>);
}

void test_positive_volatile_both_slots() {
    static_assert( extract::PipelineStage<&ps_test::f_both_volatile>);
}

void test_positive_pointer_payload_pass_through() {
    // Pointer-payload pipeline stage — D05/D06 admit because P
    // deduces to int*.  Both extractors yield int*; value-preserving.
    static_assert( extract::PipelineStage<
        &ps_test::f_pointer_pass_through>);
    static_assert(std::is_same_v<
        extract::pipeline_stage_input_value_t<
            &ps_test::f_pointer_pass_through>,
        int*>);
    static_assert(std::is_same_v<
        extract::pipeline_stage_output_value_t<
            &ps_test::f_pointer_pass_through>,
        int*>);
    static_assert(extract::pipeline_stage_is_value_preserving_v<
        &ps_test::f_pointer_pass_through>);
}

void test_positive_extras_handles_admitted() {
    // D05/D06 detect publish/pop only.  Extra unrelated methods on
    // either handle don't break recognition.
    static_assert( extract::PipelineStage<&ps_test::f_extras_handles>);
}

void test_positive_both_slots_non_noexcept() {
    // Both decomp branches active simultaneously — admitted.
    static_assert( extract::PipelineStage<&ps_test::f_both_no_noexcept>);
}

void test_negative_arity_mismatch() {
    static_assert(!extract::PipelineStage<&ps_test::f_no_param>);
    static_assert(!extract::PipelineStage<&ps_test::f_one_consumer>);
    static_assert(!extract::PipelineStage<&ps_test::f_three_params>);
}

void test_negative_consumer_not_rvalue_ref() {
    static_assert(!extract::PipelineStage<&ps_test::f_consumer_lvalue_ref>);
    static_assert(!extract::PipelineStage<
        &ps_test::f_consumer_const_rvalue_ref>);
}

void test_negative_producer_not_rvalue_ref() {
    static_assert(!extract::PipelineStage<&ps_test::f_producer_lvalue_ref>);
    static_assert(!extract::PipelineStage<
        &ps_test::f_producer_const_rvalue_ref>);
}

void test_negative_swapped_slot_order() {
    // (producer, consumer) — wrong order.  IsConsumerHandle on slot
    // 0 fails because producer has try_push, not try_pop.  This is
    // load-bearing: pipeline data flow direction is fixed by the
    // canonical shape.
    static_assert(!extract::PipelineStage<&ps_test::f_swapped_slot_order>);
}

void test_negative_hybrid_handles() {
    // D05 and D06 BOTH reject hybrids (try_push xor try_pop, never
    // both).  PipelineStage inherits the rejection from either side.
    static_assert(!extract::PipelineStage<&ps_test::f_hybrid_consumer_slot>);
    static_assert(!extract::PipelineStage<&ps_test::f_hybrid_producer_slot>);
}

void test_negative_swmr_handles_rejected() {
    // SWMR writer has publish (not try_push) → IsProducerHandle false.
    // SWMR reader has load (not try_pop) → IsConsumerHandle false.
    static_assert(!extract::PipelineStage<
        &ps_test::f_swmr_writer_in_producer_slot>);
    static_assert(!extract::PipelineStage<
        &ps_test::f_swmr_reader_in_consumer_slot>);
}

void test_negative_owned_region_rejected() {
    // OwnedRegion is not handle-shaped — neither D05 nor D06
    // recognises it.
    static_assert(!extract::PipelineStage<
        &ps_test::f_region_in_consumer_slot>);
    static_assert(!extract::PipelineStage<
        &ps_test::f_region_in_producer_slot>);
}

void test_negative_non_handle_slot() {
    static_assert(!extract::PipelineStage<&ps_test::f_int_in_consumer_slot>);
    static_assert(!extract::PipelineStage<&ps_test::f_int_in_producer_slot>);
}

void test_negative_non_void_return() {
    static_assert(!extract::PipelineStage<&ps_test::f_int_return>);
}

void test_negative_cv_qualified_handle_methods() {
    // D05/D06's signature_decomp specialisations match mptr types
    // WITHOUT cv-ref qualifiers on the method.  Const-qualified
    // try_pop / try_push produces `(C::*)(...) const [noexcept]`
    // which doesn't match either branch — D05/D06 reject, D19
    // inherits the rejection from either side.
    static_assert(!extract::PipelineStage<
        &ps_test::f_const_pop_in_consumer_slot>);
    static_assert(!extract::PipelineStage<
        &ps_test::f_const_push_in_producer_slot>);
}

void test_input_value_extraction() {
    static_assert(std::is_same_v<
        extract::pipeline_stage_input_value_t<&ps_test::f_pass_through>,
        int>);
    static_assert(std::is_same_v<
        extract::pipeline_stage_input_value_t<&ps_test::f_transform>,
        int>);
    static_assert(std::is_same_v<
        extract::pipeline_stage_input_value_t<
            &ps_test::f_transform_reverse>,
        float>);
}

void test_output_value_extraction() {
    static_assert(std::is_same_v<
        extract::pipeline_stage_output_value_t<&ps_test::f_pass_through>,
        int>);
    static_assert(std::is_same_v<
        extract::pipeline_stage_output_value_t<&ps_test::f_transform>,
        float>);
    static_assert(std::is_same_v<
        extract::pipeline_stage_output_value_t<
            &ps_test::f_transform_reverse>,
        int>);
}

void test_value_preserving_predicate() {
    // Pass-through: input == output → preserving.
    static_assert(extract::pipeline_stage_is_value_preserving_v<
        &ps_test::f_pass_through>);
    // Transform: input != output → not preserving.
    static_assert(!extract::pipeline_stage_is_value_preserving_v<
        &ps_test::f_transform>);
    static_assert(!extract::pipeline_stage_is_value_preserving_v<
        &ps_test::f_transform_reverse>);
}

void test_concept_form_in_constraints() {
    auto callable_with_pipeline = []<auto FnPtr>()
        requires extract::PipelineStage<FnPtr>
    {
        return true;
    };

    EXPECT_TRUE(callable_with_pipeline.template operator()<
        &ps_test::f_pass_through>());
    EXPECT_TRUE(callable_with_pipeline.template operator()<
        &ps_test::f_transform>());
}

void test_cross_shape_exclusion_full_matrix() {
    // PipelineStage must NOT collide with any other canonical shape.

    static_assert( extract::PipelineStage<&ps_test::f_pass_through>);
    static_assert(!extract::ConsumerEndpoint<&ps_test::f_pass_through>);
    static_assert(!extract::ProducerEndpoint<&ps_test::f_pass_through>);
    static_assert(!extract::SwmrWriter<&ps_test::f_pass_through>);
    static_assert(!extract::SwmrReader<&ps_test::f_pass_through>);
    static_assert(!extract::UnaryTransform<&ps_test::f_pass_through>);
    static_assert(!extract::BinaryTransform<&ps_test::f_pass_through>);
}

void test_inferred_tags_empty_set() {
    // D11 harvests OwnedRegion tags only; PipelineStage has none.
    namespace proto = ::crucible::safety::proto;

    using Expected = proto::PermSet<>;
    static_assert(proto::perm_set_equal_v<
        extract::inferred_permission_tags_t<&ps_test::f_pass_through>,
        Expected>);
    static_assert(extract::inferred_permission_tags_count_v<
        &ps_test::f_pass_through> == 0);
    static_assert(extract::is_tag_free_function_v<
        &ps_test::f_pass_through>);
}

void test_runtime_consistency() {
    volatile std::size_t const cap = 50;
    bool baseline_pos =
        extract::is_pipeline_stage_v<&ps_test::f_pass_through>;
    bool baseline_neg =
        !extract::is_pipeline_stage_v<&ps_test::f_one_consumer>;
    EXPECT_TRUE(baseline_pos);
    EXPECT_TRUE(baseline_neg);
    for (std::size_t i = 0; i < cap; ++i) {
        EXPECT_TRUE(baseline_pos
            == extract::is_pipeline_stage_v<&ps_test::f_pass_through>);
        EXPECT_TRUE(baseline_neg
            == !extract::is_pipeline_stage_v<&ps_test::f_one_consumer>);
        EXPECT_TRUE(extract::PipelineStage<&ps_test::f_pass_through>);
        EXPECT_TRUE(!extract::PipelineStage<&ps_test::f_no_param>);
    }
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_pipeline_stage:\n");
    run_test("test_runtime_smoke", test_runtime_smoke);
    run_test("test_positive_pass_through", test_positive_pass_through);
    run_test("test_positive_transform", test_positive_transform);
    run_test("test_positive_transform_reverse",
             test_positive_transform_reverse);
    run_test("test_positive_non_noexcept_handles",
             test_positive_non_noexcept_handles);
    run_test("test_positive_volatile_either_slot",
             test_positive_volatile_either_slot);
    run_test("test_positive_volatile_both_slots",
             test_positive_volatile_both_slots);
    run_test("test_positive_pointer_payload_pass_through",
             test_positive_pointer_payload_pass_through);
    run_test("test_positive_extras_handles_admitted",
             test_positive_extras_handles_admitted);
    run_test("test_positive_both_slots_non_noexcept",
             test_positive_both_slots_non_noexcept);
    run_test("test_negative_arity_mismatch",
             test_negative_arity_mismatch);
    run_test("test_negative_consumer_not_rvalue_ref",
             test_negative_consumer_not_rvalue_ref);
    run_test("test_negative_producer_not_rvalue_ref",
             test_negative_producer_not_rvalue_ref);
    run_test("test_negative_swapped_slot_order",
             test_negative_swapped_slot_order);
    run_test("test_negative_hybrid_handles",
             test_negative_hybrid_handles);
    run_test("test_negative_swmr_handles_rejected",
             test_negative_swmr_handles_rejected);
    run_test("test_negative_owned_region_rejected",
             test_negative_owned_region_rejected);
    run_test("test_negative_non_handle_slot",
             test_negative_non_handle_slot);
    run_test("test_negative_non_void_return",
             test_negative_non_void_return);
    run_test("test_negative_cv_qualified_handle_methods",
             test_negative_cv_qualified_handle_methods);
    run_test("test_input_value_extraction",
             test_input_value_extraction);
    run_test("test_output_value_extraction",
             test_output_value_extraction);
    run_test("test_value_preserving_predicate",
             test_value_preserving_predicate);
    run_test("test_concept_form_in_constraints",
             test_concept_form_in_constraints);
    run_test("test_cross_shape_exclusion_full_matrix",
             test_cross_shape_exclusion_full_matrix);
    run_test("test_inferred_tags_empty_set",
             test_inferred_tags_empty_set);
    run_test("test_runtime_consistency",
             test_runtime_consistency);
    std::fprintf(stderr, "\n%d passed, %d failed\n",
                 total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
