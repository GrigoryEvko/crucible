#include <crucible/safety/ConsumerEndpoint.h>

#include <crucible/safety/BinaryTransform.h>
#include <crucible/safety/InferredPermissionTags.h>
#include <crucible/safety/OwnedRegion.h>
#include <crucible/safety/ProducerEndpoint.h>
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
struct out_tag {};

}  // namespace

namespace ce_test {

struct consumer_handle_int {
    [[nodiscard]] std::optional<int> try_pop() noexcept { return 0; }
};

struct consumer_handle_float {
    [[nodiscard]] std::optional<float> try_pop() noexcept { return 0.0f; }
};

struct producer_handle_int {
    [[nodiscard]] bool try_push(int const&) noexcept { return true; }
};

struct hybrid_handle {
    [[nodiscard]] bool try_push(int const&) noexcept { return true; }
    [[nodiscard]] std::optional<int> try_pop() noexcept { return 0; }
};

struct consumer_handle_int_returning {
    [[nodiscard]] int try_pop() noexcept { return 0; }
};

struct consumer_handle_int_no_noexcept {
    [[nodiscard]] std::optional<int> try_pop() { return 0; }
};

struct consumer_handle_const_pop {
    [[nodiscard]] std::optional<int> try_pop() const noexcept { return 0; }
};

struct consumer_handle_rref_pop {
    [[nodiscard]] std::optional<int> try_pop() && noexcept { return 0; }
};

using OR_int_out = ::crucible::safety::OwnedRegion<int, ::out_tag>;
using OR_float_out = ::crucible::safety::OwnedRegion<float, ::out_tag>;
using OR_int_in = ::crucible::safety::OwnedRegion<int, ::in_tag>;

void f_well_formed(consumer_handle_int&&, OR_int_out&&) noexcept;

void f_value_mismatch(consumer_handle_int&&, OR_float_out&&) noexcept;

void f_well_formed_float(consumer_handle_float&&, OR_float_out&&) noexcept;

void f_handle_volatile(consumer_handle_int volatile&&, OR_int_out&&) noexcept;

void f_region_volatile(consumer_handle_int&&, OR_int_out volatile&&) noexcept;

void f_both_volatile(consumer_handle_int volatile&&, OR_int_out volatile&&) noexcept;

void f_no_param() noexcept;
void f_one_param(consumer_handle_int&&) noexcept;
void f_three_params(consumer_handle_int&&, OR_int_out&&, int) noexcept;

void f_handle_lvalue_ref(consumer_handle_int&, OR_int_out&&) noexcept;

void f_handle_const_rvalue_ref(consumer_handle_int const&&, OR_int_out&&) noexcept;

void f_region_lvalue_ref(consumer_handle_int&&, OR_int_out&) noexcept;

void f_region_const_rvalue_ref(consumer_handle_int&&, OR_int_out const&&) noexcept;

void f_int_in_handle_slot(int, OR_int_out&&) noexcept;

void f_ptr_to_handle(consumer_handle_int*, OR_int_out&&) noexcept;

void f_producer_in_consumer_slot(producer_handle_int&&, OR_int_out&&) noexcept;

void f_hybrid_in_consumer_slot(hybrid_handle&&, OR_int_out&&) noexcept;

void f_int_returning_pop_in_slot(consumer_handle_int_returning&&, OR_int_out&&) noexcept;

void f_int_in_region_slot(consumer_handle_int&&, int) noexcept;

int f_int_return(consumer_handle_int&&, OR_int_out&&) noexcept;

OR_int_in f_region_return(consumer_handle_int&&, OR_int_out&&) noexcept;

void f_two_regions(OR_int_in&&, OR_int_out&&) noexcept;

void f_no_noexcept_consumer(consumer_handle_int_no_noexcept&&, OR_int_out&&) noexcept;

void f_const_pop_in_consumer_slot(consumer_handle_const_pop&&, OR_int_out&&) noexcept;

void f_rref_pop_in_consumer_slot(consumer_handle_rref_pop&&, OR_int_out&&) noexcept;

}  // namespace ce_test

namespace {

void test_runtime_smoke() { EXPECT_TRUE(extract::consumer_endpoint_smoke_test()); }

void test_positive_well_formed() {
    static_assert(extract::ConsumerEndpoint<&ce_test::f_well_formed>);
    static_assert(extract::is_consumer_endpoint_v<&ce_test::f_well_formed>);
    static_assert(extract::ConsumerEndpoint<&ce_test::f_well_formed_float>);
}

void test_positive_value_mismatch_admitted_by_concept() {
    static_assert(extract::ConsumerEndpoint<&ce_test::f_value_mismatch>);
    static_assert(extract::is_consumer_endpoint_v<&ce_test::f_value_mismatch>);
}

void test_negative_arity_mismatch() {
    static_assert(!extract::ConsumerEndpoint<&ce_test::f_no_param>);
    static_assert(!extract::ConsumerEndpoint<&ce_test::f_one_param>);
    static_assert(!extract::ConsumerEndpoint<&ce_test::f_three_params>);
}

void test_negative_handle_not_rvalue_ref() { static_assert(!extract::ConsumerEndpoint<&ce_test::f_handle_lvalue_ref>); }

void test_negative_handle_const_rvalue_ref() {
    static_assert(!extract::ConsumerEndpoint<&ce_test::f_handle_const_rvalue_ref>);
}

void test_negative_region_not_rvalue_ref() { static_assert(!extract::ConsumerEndpoint<&ce_test::f_region_lvalue_ref>); }

void test_negative_region_const_rvalue_ref() {
    static_assert(!extract::ConsumerEndpoint<&ce_test::f_region_const_rvalue_ref>);
}

void test_negative_handle_slot_not_handle() {
    static_assert(!extract::ConsumerEndpoint<&ce_test::f_int_in_handle_slot>);
    static_assert(!extract::ConsumerEndpoint<&ce_test::f_ptr_to_handle>);
}

void test_negative_producer_in_consumer_slot() {
    static_assert(!extract::ConsumerEndpoint<&ce_test::f_producer_in_consumer_slot>);
}

void test_negative_hybrid_in_consumer_slot() {
    static_assert(!extract::ConsumerEndpoint<&ce_test::f_hybrid_in_consumer_slot>);
}

void test_negative_int_returning_pop() {
    static_assert(!extract::ConsumerEndpoint<&ce_test::f_int_returning_pop_in_slot>);
}

void test_positive_non_noexcept_try_pop() {
    // The only witness here whose try_pop is not noexcept.  Every
    // other consumer witness covers the noexcept form.
    static_assert(extract::ConsumerEndpoint<&ce_test::f_no_noexcept_consumer>);
    static_assert(std::is_same_v<extract::consumer_endpoint_handle_value_t<&ce_test::f_no_noexcept_consumer>, int>);
}

void test_negative_cv_qualified_try_pop() {
    // Rejected, unlike the missing-noexcept witness above.
    static_assert(!extract::ConsumerEndpoint<&ce_test::f_const_pop_in_consumer_slot>);

    static_assert(!extract::ConsumerEndpoint<&ce_test::f_rref_pop_in_consumer_slot>);
}

void test_negative_region_slot_not_region() {
    static_assert(!extract::ConsumerEndpoint<&ce_test::f_int_in_region_slot>);
}

void test_negative_non_void_return() {
    static_assert(!extract::ConsumerEndpoint<&ce_test::f_int_return>);
    static_assert(!extract::ConsumerEndpoint<&ce_test::f_region_return>);
}

void test_negative_two_regions_is_binary() {
    static_assert(!extract::ConsumerEndpoint<&ce_test::f_two_regions>);
    static_assert(extract::BinaryTransform<&ce_test::f_two_regions>);
}

void test_volatile_admitted_on_either_or_both() {
    static_assert(extract::ConsumerEndpoint<&ce_test::f_handle_volatile>);
    static_assert(extract::ConsumerEndpoint<&ce_test::f_region_volatile>);
    static_assert(extract::ConsumerEndpoint<&ce_test::f_both_volatile>);
}

void test_handle_value_extraction() {
    static_assert(std::is_same_v<extract::consumer_endpoint_handle_value_t<&ce_test::f_well_formed>, int>);
    static_assert(std::is_same_v<extract::consumer_endpoint_handle_value_t<&ce_test::f_well_formed_float>, float>);
    static_assert(std::is_same_v<extract::consumer_endpoint_handle_value_t<&ce_test::f_value_mismatch>, int>);
}

void test_region_tag_extraction() {
    static_assert(std::is_same_v<extract::consumer_endpoint_region_tag_t<&ce_test::f_well_formed>, out_tag>);
    static_assert(std::is_same_v<extract::consumer_endpoint_region_tag_t<&ce_test::f_value_mismatch>, out_tag>);
    static_assert(std::is_same_v<extract::consumer_endpoint_region_tag_t<&ce_test::f_well_formed_float>, out_tag>);
}

void test_region_value_extraction() {
    static_assert(std::is_same_v<extract::consumer_endpoint_region_value_t<&ce_test::f_well_formed>, int>);
    static_assert(std::is_same_v<extract::consumer_endpoint_region_value_t<&ce_test::f_well_formed_float>, float>);
    static_assert(std::is_same_v<extract::consumer_endpoint_region_value_t<&ce_test::f_value_mismatch>, float>);
}

void test_value_consistency_predicate() {
    static_assert(extract::consumer_endpoint_value_consistent_v<&ce_test::f_well_formed>);
    static_assert(extract::consumer_endpoint_value_consistent_v<&ce_test::f_well_formed_float>);
    static_assert(!extract::consumer_endpoint_value_consistent_v<&ce_test::f_value_mismatch>);
}

void test_concept_form_in_constraints() {
    auto callable_with_endpoint = []<auto FnPtr>()
        requires extract::ConsumerEndpoint<FnPtr>
    { return true; };

    EXPECT_TRUE(callable_with_endpoint.template operator()<&ce_test::f_well_formed>());
    EXPECT_TRUE(callable_with_endpoint.template operator()<&ce_test::f_well_formed_float>());
    EXPECT_TRUE(callable_with_endpoint.template operator()<&ce_test::f_value_mismatch>());
}

void test_cross_shape_exclusion_full_matrix() {
    static_assert(extract::ConsumerEndpoint<&ce_test::f_well_formed>);
    static_assert(!extract::ProducerEndpoint<&ce_test::f_well_formed>);
    static_assert(!extract::UnaryTransform<&ce_test::f_well_formed>);
    static_assert(!extract::BinaryTransform<&ce_test::f_well_formed>);

    static_assert(!extract::ConsumerEndpoint<&ce_test::f_two_regions>);
    static_assert(!extract::ProducerEndpoint<&ce_test::f_two_regions>);
    static_assert(!extract::UnaryTransform<&ce_test::f_two_regions>);
    static_assert(extract::BinaryTransform<&ce_test::f_two_regions>);

    static_assert(!extract::ConsumerEndpoint<&ce_test::f_producer_in_consumer_slot>);
    static_assert(extract::ProducerEndpoint<&ce_test::f_producer_in_consumer_slot>);
}

void test_inferred_tags_harvests_region_tag_only() {
    namespace proto = ::crucible::safety::proto;

    using Expected = proto::PermSet<out_tag>;
    static_assert(proto::perm_set_equal_v<extract::inferred_permission_tags_t<&ce_test::f_well_formed>, Expected>);

    static_assert(extract::inferred_permission_tags_count_v<&ce_test::f_well_formed> == 1);
    static_assert(extract::function_has_tag_v<&ce_test::f_well_formed, out_tag>);
    static_assert(!extract::function_has_tag_v<&ce_test::f_well_formed, in_tag>);
}

void test_runtime_consistency() {
    volatile std::size_t const cap = 50;
    bool baseline_pos = extract::is_consumer_endpoint_v<&ce_test::f_well_formed>;
    bool baseline_neg = !extract::is_consumer_endpoint_v<&ce_test::f_one_param>;
    EXPECT_TRUE(baseline_pos);
    EXPECT_TRUE(baseline_neg);
    for (std::size_t i = 0; i < cap; ++i) {
        EXPECT_TRUE(baseline_pos == extract::is_consumer_endpoint_v<&ce_test::f_well_formed>);
        EXPECT_TRUE(baseline_neg == !extract::is_consumer_endpoint_v<&ce_test::f_one_param>);
        EXPECT_TRUE(extract::ConsumerEndpoint<&ce_test::f_well_formed>);
        EXPECT_TRUE(!extract::ConsumerEndpoint<&ce_test::f_no_param>);
    }
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_consumer_endpoint:\n");
    run_test("test_runtime_smoke", test_runtime_smoke);
    run_test("test_positive_well_formed", test_positive_well_formed);
    run_test("test_positive_value_mismatch_admitted_by_concept", test_positive_value_mismatch_admitted_by_concept);
    run_test("test_negative_arity_mismatch", test_negative_arity_mismatch);
    run_test("test_negative_handle_not_rvalue_ref", test_negative_handle_not_rvalue_ref);
    run_test("test_negative_handle_const_rvalue_ref", test_negative_handle_const_rvalue_ref);
    run_test("test_negative_region_not_rvalue_ref", test_negative_region_not_rvalue_ref);
    run_test("test_negative_region_const_rvalue_ref", test_negative_region_const_rvalue_ref);
    run_test("test_negative_handle_slot_not_handle", test_negative_handle_slot_not_handle);
    run_test("test_negative_producer_in_consumer_slot", test_negative_producer_in_consumer_slot);
    run_test("test_negative_hybrid_in_consumer_slot", test_negative_hybrid_in_consumer_slot);
    run_test("test_negative_int_returning_pop", test_negative_int_returning_pop);
    run_test("test_positive_non_noexcept_try_pop", test_positive_non_noexcept_try_pop);
    run_test("test_negative_cv_qualified_try_pop", test_negative_cv_qualified_try_pop);
    run_test("test_negative_region_slot_not_region", test_negative_region_slot_not_region);
    run_test("test_negative_non_void_return", test_negative_non_void_return);
    run_test("test_negative_two_regions_is_binary", test_negative_two_regions_is_binary);
    run_test("test_volatile_admitted_on_either_or_both", test_volatile_admitted_on_either_or_both);
    run_test("test_handle_value_extraction", test_handle_value_extraction);
    run_test("test_region_tag_extraction", test_region_tag_extraction);
    run_test("test_region_value_extraction", test_region_value_extraction);
    run_test("test_value_consistency_predicate", test_value_consistency_predicate);
    run_test("test_concept_form_in_constraints", test_concept_form_in_constraints);
    run_test("test_cross_shape_exclusion_full_matrix", test_cross_shape_exclusion_full_matrix);
    run_test("test_inferred_tags_harvests_region_tag_only", test_inferred_tags_harvests_region_tag_only);
    run_test("test_runtime_consistency", test_runtime_consistency);
    std::fprintf(stderr, "\n%d passed, %d failed\n", total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
