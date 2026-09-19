// A pipeline stage is a function that drains one handle and pushes into
// another:
//
//   void stage(ConsumerHandle&&, ProducerHandle&&)
//
// The slot order is part of the shape, not a convention.  Consumer
// first, producer second is what fixes the direction data flows in, and
// a stage written the other way round has to be rejected rather than
// silently run backwards.
//
// Handle recognition is structural: a consumer is anything with a
// try_pop and no try_push, a producer the reverse.  That makes the
// interesting cases the ones that nearly match, so most of the
// witnesses below exist to be refused.

#include <crucible/safety/PipelineStage.h>

#include <crucible/safety/BinaryTransform.h>
#include <crucible/safety/ConsumerEndpoint.h>
#include <crucible/safety/InferredPermissionTags.h>
#include <crucible/safety/_OwnedRegion.h>
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

#define EXPECT_TRUE(cond)                                                                            \
    do {                                                                                             \
        if (!(cond)) {                                                                               \
            std::fprintf(stderr, "    EXPECT_TRUE failed: %s (%s:%d)\n", #cond, __FILE__, __LINE__); \
            throw TestFailure{};                                                                     \
        }                                                                                            \
    } while (0)

namespace extract = ::crucible::safety::extract;
namespace safety = ::crucible::safety;

struct in_tag {};

}  // namespace

namespace ps_test {

struct consumer_int {
    [[nodiscard]] std::optional<int> try_pop() noexcept { return 0; }
};
struct consumer_float {
    [[nodiscard]] std::optional<float> try_pop() noexcept { return 0.0f; }
};
struct consumer_int_no_noexcept {
    [[nodiscard]] std::optional<int> try_pop() { return 0; }
};

struct producer_int {
    [[nodiscard]] bool try_push(int const&) noexcept { return true; }
};
struct producer_float {
    [[nodiscard]] bool try_push(float const&) noexcept { return true; }
};
struct producer_int_no_noexcept {
    [[nodiscard]] bool try_push(int const&) { return true; }
};

// Carries both operations, so it is neither a consumer nor a producer.
struct hybrid_handle {
    [[nodiscard]] bool try_push(int const&) noexcept { return true; }
    [[nodiscard]] std::optional<int> try_pop() noexcept { return 0; }
};

// Publish and load rather than push and pop: the same roles under
// different names, which recognition must not accept.
struct swmr_writer {
    void publish(int const&) noexcept {}
};
struct swmr_reader {
    int load() const noexcept { return 0; }
};

// The payload type is deduced, so a pointer payload is ordinary.
struct consumer_ptr {
    [[nodiscard]] std::optional<int*> try_pop() noexcept { return nullptr; }
};
struct producer_ptr {
    [[nodiscard]] bool try_push(int* const&) noexcept { return true; }
};

// Recognition looks only at the one operation, so unrelated members
// neither help nor hinder.
struct consumer_with_extras {
    [[nodiscard]] std::optional<int> try_pop() noexcept { return 0; }
    void unrelated_helper() noexcept {}
    int another_method(int) const noexcept { return 0; }
};
struct producer_with_extras {
    [[nodiscard]] bool try_push(int const&) noexcept { return true; }
    void unrelated_helper() noexcept {}
};

// Recognition matches member-pointer types without cv qualifiers on the
// method, so a const try_pop or try_push does not match at all.  The
// rejection is a consequence of how the match is written rather than a
// deliberate rule about constness.
struct consumer_const_pop {
    [[nodiscard]] std::optional<int> try_pop() const noexcept { return 0; }
};

struct producer_const_push {
    [[nodiscard]] bool try_push(int const&) const noexcept { return true; }
};

using OR_int = ::crucible::safety::OwnedRegion<int, ::in_tag>;

void f_pass_through(consumer_int&&, producer_int&&) noexcept;
void f_transform(consumer_int&&, producer_float&&) noexcept;
void f_transform_reverse(consumer_float&&, producer_int&&) noexcept;

void f_consumer_no_noexcept(consumer_int_no_noexcept&&, producer_int&&) noexcept;
void f_producer_no_noexcept(consumer_int&&, producer_int_no_noexcept&&) noexcept;

void f_consumer_volatile(consumer_int volatile&&, producer_int&&) noexcept;
void f_producer_volatile(consumer_int&&, producer_int volatile&&) noexcept;
void f_both_volatile(consumer_int volatile&&, producer_int volatile&&) noexcept;

void f_pointer_pass_through(consumer_ptr&&, producer_ptr&&) noexcept;
void f_extras_handles(consumer_with_extras&&, producer_with_extras&&) noexcept;
void f_both_no_noexcept(consumer_int_no_noexcept&&, producer_int_no_noexcept&&) noexcept;

// More than one handle per direction.  These satisfy the variadic
// concept and not the fixed one-in one-out concept, which is the only
// thing separating the two.
void f_fan_in_3_to_1(consumer_int&&, consumer_int&&, consumer_float&&, producer_int&&) noexcept;
void f_fan_out_1_to_2(consumer_int&&, producer_int&&, producer_float&&) noexcept;
void f_variadic_interleaved(consumer_int&&, producer_int&&, consumer_float&&) noexcept;

void f_no_param() noexcept;
void f_one_consumer(consumer_int&&) noexcept;
void f_three_params(consumer_int&&, producer_int&&, int) noexcept;

// A handle must be consumed, so neither a borrow nor a const rvalue
// will do in either slot.
void f_consumer_lvalue_ref(consumer_int&, producer_int&&) noexcept;
void f_consumer_const_rvalue_ref(consumer_int const&&, producer_int&&) noexcept;
void f_producer_lvalue_ref(consumer_int&&, producer_int&) noexcept;
void f_producer_const_rvalue_ref(consumer_int&&, producer_int const&&) noexcept;

// Both handles are well-formed, and the stage is still refused: the
// slot each one sits in is what decides the direction.
void f_swapped_slot_order(producer_int&&, consumer_int&&) noexcept;

void f_hybrid_consumer_slot(hybrid_handle&&, producer_int&&) noexcept;
void f_hybrid_producer_slot(consumer_int&&, hybrid_handle&&) noexcept;

void f_swmr_writer_in_producer_slot(consumer_int&&, swmr_writer&&) noexcept;
void f_swmr_reader_in_consumer_slot(swmr_reader&&, producer_int&&) noexcept;

void f_region_in_consumer_slot(OR_int&&, producer_int&&) noexcept;
void f_region_in_producer_slot(consumer_int&&, OR_int&&) noexcept;

void f_int_in_consumer_slot(int, producer_int&&) noexcept;
void f_int_in_producer_slot(consumer_int&&, int) noexcept;

int f_int_return(consumer_int&&, producer_int&&) noexcept;

void f_const_pop_in_consumer_slot(consumer_const_pop&&, producer_int&&) noexcept;
void f_const_push_in_producer_slot(consumer_int&&, producer_const_push&&) noexcept;

}  // namespace ps_test

namespace {

void test_runtime_smoke() { EXPECT_TRUE(extract::pipeline_stage_smoke_test()); }

void test_positive_pass_through() {
    static_assert(extract::PipelineStage<&ps_test::f_pass_through>);
    static_assert(extract::is_pipeline_stage_v<&ps_test::f_pass_through>);
    static_assert(extract::pipeline_stage_is_value_preserving_v<&ps_test::f_pass_through>);
}

void test_positive_transform() {
    // The concept admits a stage whose payload type changes.  Whether
    // it changes is a separate predicate, not a condition of being a
    // stage at all.
    static_assert(extract::PipelineStage<&ps_test::f_transform>);
    static_assert(!extract::pipeline_stage_is_value_preserving_v<&ps_test::f_transform>);
}

void test_positive_transform_reverse() {
    // The payload types carry no ordering of their own, so reversing
    // them is still a stage.
    static_assert(extract::PipelineStage<&ps_test::f_transform_reverse>);
    static_assert(!extract::pipeline_stage_is_value_preserving_v<&ps_test::f_transform_reverse>);
}

void test_positive_non_noexcept_handles() {
    static_assert(extract::PipelineStage<&ps_test::f_consumer_no_noexcept>);
    static_assert(extract::PipelineStage<&ps_test::f_producer_no_noexcept>);
}

void test_positive_volatile_either_slot() {
    static_assert(extract::PipelineStage<&ps_test::f_consumer_volatile>);
    static_assert(extract::PipelineStage<&ps_test::f_producer_volatile>);
}

void test_positive_volatile_both_slots() { static_assert(extract::PipelineStage<&ps_test::f_both_volatile>); }

void test_positive_pointer_payload_pass_through() {
    static_assert(extract::PipelineStage<&ps_test::f_pointer_pass_through>);
    static_assert(std::is_same_v<extract::pipeline_stage_input_value_t<&ps_test::f_pointer_pass_through>, int*>);
    static_assert(std::is_same_v<extract::pipeline_stage_output_value_t<&ps_test::f_pointer_pass_through>, int*>);
    static_assert(extract::pipeline_stage_is_value_preserving_v<&ps_test::f_pointer_pass_through>);
}

void test_positive_extras_handles_admitted() { static_assert(extract::PipelineStage<&ps_test::f_extras_handles>); }

void test_positive_both_slots_non_noexcept() { static_assert(extract::PipelineStage<&ps_test::f_both_no_noexcept>); }

void test_negative_arity_mismatch() {
    static_assert(!extract::PipelineStage<&ps_test::f_no_param>);
    static_assert(!extract::PipelineStage<&ps_test::f_one_consumer>);
    static_assert(!extract::PipelineStage<&ps_test::f_three_params>);
}

void test_negative_consumer_not_rvalue_ref() {
    static_assert(!extract::PipelineStage<&ps_test::f_consumer_lvalue_ref>);
    static_assert(!extract::PipelineStage<&ps_test::f_consumer_const_rvalue_ref>);
}

void test_negative_producer_not_rvalue_ref() {
    static_assert(!extract::PipelineStage<&ps_test::f_producer_lvalue_ref>);
    static_assert(!extract::PipelineStage<&ps_test::f_producer_const_rvalue_ref>);
}

void test_negative_swapped_slot_order() { static_assert(!extract::PipelineStage<&ps_test::f_swapped_slot_order>); }

void test_negative_hybrid_handles() {
    static_assert(!extract::PipelineStage<&ps_test::f_hybrid_consumer_slot>);
    static_assert(!extract::PipelineStage<&ps_test::f_hybrid_producer_slot>);
}

void test_negative_swmr_handles_rejected() {
    static_assert(!extract::PipelineStage<&ps_test::f_swmr_writer_in_producer_slot>);
    static_assert(!extract::PipelineStage<&ps_test::f_swmr_reader_in_consumer_slot>);
}

void test_negative_owned_region_rejected() {
    static_assert(!extract::PipelineStage<&ps_test::f_region_in_consumer_slot>);
    static_assert(!extract::PipelineStage<&ps_test::f_region_in_producer_slot>);
}

void test_negative_non_handle_slot() {
    static_assert(!extract::PipelineStage<&ps_test::f_int_in_consumer_slot>);
    static_assert(!extract::PipelineStage<&ps_test::f_int_in_producer_slot>);
}

void test_negative_non_void_return() { static_assert(!extract::PipelineStage<&ps_test::f_int_return>); }

void test_negative_cv_qualified_handle_methods() {
    static_assert(!extract::PipelineStage<&ps_test::f_const_pop_in_consumer_slot>);
    static_assert(!extract::PipelineStage<&ps_test::f_const_push_in_producer_slot>);
}

void test_input_value_extraction() {
    static_assert(std::is_same_v<extract::pipeline_stage_input_value_t<&ps_test::f_pass_through>, int>);
    static_assert(std::is_same_v<extract::pipeline_stage_input_value_t<&ps_test::f_transform>, int>);
    static_assert(std::is_same_v<extract::pipeline_stage_input_value_t<&ps_test::f_transform_reverse>, float>);
}

void test_output_value_extraction() {
    static_assert(std::is_same_v<extract::pipeline_stage_output_value_t<&ps_test::f_pass_through>, int>);
    static_assert(std::is_same_v<extract::pipeline_stage_output_value_t<&ps_test::f_transform>, float>);
    static_assert(std::is_same_v<extract::pipeline_stage_output_value_t<&ps_test::f_transform_reverse>, int>);
}

void test_value_preserving_predicate() {
    static_assert(extract::pipeline_stage_is_value_preserving_v<&ps_test::f_pass_through>);
    static_assert(!extract::pipeline_stage_is_value_preserving_v<&ps_test::f_transform>);
    static_assert(!extract::pipeline_stage_is_value_preserving_v<&ps_test::f_transform_reverse>);
}

void test_variadic_stage_arity() {
    static_assert(extract::VariadicPipelineStage<&ps_test::f_fan_in_3_to_1>);
    static_assert(!extract::PipelineStage<&ps_test::f_fan_in_3_to_1>);
    static_assert(extract::StageArity<&ps_test::f_fan_in_3_to_1>::input_count == 3);
    static_assert(extract::StageArity<&ps_test::f_fan_in_3_to_1>::output_count == 1);

    static_assert(extract::VariadicPipelineStage<&ps_test::f_fan_out_1_to_2>);
    static_assert(!extract::PipelineStage<&ps_test::f_fan_out_1_to_2>);
    static_assert(extract::StageArity<&ps_test::f_fan_out_1_to_2>::input_count == 1);
    static_assert(extract::StageArity<&ps_test::f_fan_out_1_to_2>::output_count == 2);

    static_assert(!extract::VariadicPipelineStage<&ps_test::f_variadic_interleaved>);
}

void test_concept_form_in_constraints() {
    auto callable_with_pipeline = []<auto FnPtr>()
        requires extract::PipelineStage<FnPtr>
    { return true; };

    EXPECT_TRUE(callable_with_pipeline.template operator()<&ps_test::f_pass_through>());
    EXPECT_TRUE(callable_with_pipeline.template operator()<&ps_test::f_transform>());
}

void test_cross_shape_exclusion_full_matrix() {
    // A function has at most one canonical shape, so the dispatcher's
    // routing is decided by the signature rather than by the order it
    // tries its concepts in.
    static_assert(extract::PipelineStage<&ps_test::f_pass_through>);
    static_assert(!extract::ConsumerEndpoint<&ps_test::f_pass_through>);
    static_assert(!extract::ProducerEndpoint<&ps_test::f_pass_through>);
    static_assert(!extract::SwmrWriter<&ps_test::f_pass_through>);
    static_assert(!extract::SwmrReader<&ps_test::f_pass_through>);
    static_assert(!extract::UnaryTransform<&ps_test::f_pass_through>);
    static_assert(!extract::BinaryTransform<&ps_test::f_pass_through>);
}

void test_inferred_tags_empty_set() {
    // Permission tags are harvested from owned regions, and a stage
    // takes none, so its tag set is empty rather than merely small.
    namespace proto = ::crucible::safety::proto;

    using Expected = proto::PermSet<>;
    static_assert(proto::perm_set_equal_v<extract::inferred_permission_tags_t<&ps_test::f_pass_through>, Expected>);
    static_assert(extract::inferred_permission_tags_count_v<&ps_test::f_pass_through> == 0);
    static_assert(extract::is_tag_free_function_v<&ps_test::f_pass_through>);
}

void test_runtime_consistency() {
    volatile std::size_t const cap = 50;
    bool baseline_pos = extract::is_pipeline_stage_v<&ps_test::f_pass_through>;
    bool baseline_neg = !extract::is_pipeline_stage_v<&ps_test::f_one_consumer>;
    EXPECT_TRUE(baseline_pos);
    EXPECT_TRUE(baseline_neg);
    for (std::size_t i = 0; i < cap; ++i) {
        EXPECT_TRUE(baseline_pos == extract::is_pipeline_stage_v<&ps_test::f_pass_through>);
        EXPECT_TRUE(baseline_neg == !extract::is_pipeline_stage_v<&ps_test::f_one_consumer>);
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
    run_test("test_positive_transform_reverse", test_positive_transform_reverse);
    run_test("test_positive_non_noexcept_handles", test_positive_non_noexcept_handles);
    run_test("test_positive_volatile_either_slot", test_positive_volatile_either_slot);
    run_test("test_positive_volatile_both_slots", test_positive_volatile_both_slots);
    run_test("test_positive_pointer_payload_pass_through", test_positive_pointer_payload_pass_through);
    run_test("test_positive_extras_handles_admitted", test_positive_extras_handles_admitted);
    run_test("test_positive_both_slots_non_noexcept", test_positive_both_slots_non_noexcept);
    run_test("test_negative_arity_mismatch", test_negative_arity_mismatch);
    run_test("test_negative_consumer_not_rvalue_ref", test_negative_consumer_not_rvalue_ref);
    run_test("test_negative_producer_not_rvalue_ref", test_negative_producer_not_rvalue_ref);
    run_test("test_negative_swapped_slot_order", test_negative_swapped_slot_order);
    run_test("test_negative_hybrid_handles", test_negative_hybrid_handles);
    run_test("test_negative_swmr_handles_rejected", test_negative_swmr_handles_rejected);
    run_test("test_negative_owned_region_rejected", test_negative_owned_region_rejected);
    run_test("test_negative_non_handle_slot", test_negative_non_handle_slot);
    run_test("test_negative_non_void_return", test_negative_non_void_return);
    run_test("test_negative_cv_qualified_handle_methods", test_negative_cv_qualified_handle_methods);
    run_test("test_input_value_extraction", test_input_value_extraction);
    run_test("test_output_value_extraction", test_output_value_extraction);
    run_test("test_value_preserving_predicate", test_value_preserving_predicate);
    run_test("test_variadic_stage_arity", test_variadic_stage_arity);
    run_test("test_concept_form_in_constraints", test_concept_form_in_constraints);
    run_test("test_cross_shape_exclusion_full_matrix", test_cross_shape_exclusion_full_matrix);
    run_test("test_inferred_tags_empty_set", test_inferred_tags_empty_set);
    run_test("test_runtime_consistency", test_runtime_consistency);
    std::fprintf(stderr, "\n%d passed, %d failed\n", total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
