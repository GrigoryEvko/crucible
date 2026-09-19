// A header reached only through other headers is never built under the
// project warning flags, and the static_asserts it embeds are never
// instantiated.  This translation unit exists to pull the
// producer-endpoint concept into a real compilation and to drive its
// runtime smoke test.
//
// The shape is a producer handle and a region, both consumed.  Handle
// recognition is structural, so the cases worth pinning are the ones
// that nearly match: a consumer handle, a handle that does both, and
// the several ways a parameter can fail to be consumed.

#include <crucible/safety/ProducerEndpoint.h>

#include <crucible/safety/BinaryTransform.h>
#include <crucible/safety/InferredPermissionTags.h>
#include <crucible/safety/_OwnedRegion.h>
#include <crucible/safety/UnaryTransform.h>

#include <cstdio>
#include <cstdlib>
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

// The handles below are declared here rather than reused from the
// handle header's own testing types, so that this test does not depend
// on something that header treats as private.

namespace pe_test {

struct producer_handle_int {
    [[nodiscard]] bool try_push(int const&) noexcept { return true; }
};

struct producer_handle_float {
    [[nodiscard]] bool try_push(float const&) noexcept { return true; }
};

struct consumer_handle_int {
    [[nodiscard]] int try_pop() noexcept { return 0; }
};

// Carries both operations, so it is neither a producer nor a consumer.
struct hybrid_handle {
    [[nodiscard]] bool try_push(int const&) noexcept { return true; }
    [[nodiscard]] int try_pop() noexcept { return 0; }
};

// Recognition has a specialization for each of the noexcept and the
// throwing form, so this one is admitted.
struct producer_handle_int_no_noexcept {
    [[nodiscard]] bool try_push(int const&) { return true; }
};

// Recognition matches member-pointer types without cv or ref
// qualifiers on the method, so neither of the next two matches at all.
struct producer_handle_const_push {
    [[nodiscard]] bool try_push(int const&) const noexcept { return true; }
};

struct producer_handle_rref_push {
    [[nodiscard]] bool try_push(int const&) && noexcept { return true; }
};

using OR_int_in = ::crucible::safety::OwnedRegion<int, ::in_tag>;
using OR_float_in = ::crucible::safety::OwnedRegion<float, ::in_tag>;
using OR_int_out = ::crucible::safety::OwnedRegion<int, ::out_tag>;

void f_well_formed(producer_handle_int&&, OR_int_in&&) noexcept;

// The handle's payload and the region's element type disagree here.
// The concept still admits it, because matching the shape is what lets
// a dispatcher report the type mismatch specifically instead of
// reporting that nothing matched.
void f_value_mismatch(producer_handle_int&&, OR_float_in&&) noexcept;

void f_well_formed_float(producer_handle_float&&, OR_float_in&&) noexcept;

void f_handle_volatile(producer_handle_int volatile&&, OR_int_in&&) noexcept;
void f_region_volatile(producer_handle_int&&, OR_int_in volatile&&) noexcept;
void f_both_volatile(producer_handle_int volatile&&, OR_int_in volatile&&) noexcept;

void f_no_param() noexcept;
void f_one_param(producer_handle_int&&) noexcept;
void f_three_params(producer_handle_int&&, OR_int_in&&, int) noexcept;

// Both parameters are consumed, so neither a borrow nor a const rvalue
// will do in either slot.
void f_handle_lvalue_ref(producer_handle_int&, OR_int_in&&) noexcept;
void f_handle_const_rvalue_ref(producer_handle_int const&&, OR_int_in&&) noexcept;
void f_region_lvalue_ref(producer_handle_int&&, OR_int_in&) noexcept;
void f_region_const_rvalue_ref(producer_handle_int&&, OR_int_in const&&) noexcept;

void f_int_in_handle_slot(int, OR_int_in&&) noexcept;

// A pointer to a handle is not a handle.
void f_ptr_to_handle(producer_handle_int*, OR_int_in&&) noexcept;

void f_consumer_in_producer_slot(consumer_handle_int&&, OR_int_in&&) noexcept;
void f_hybrid_in_producer_slot(hybrid_handle&&, OR_int_in&&) noexcept;

void f_int_in_region_slot(producer_handle_int&&, int) noexcept;

int f_int_return(producer_handle_int&&, OR_int_in&&) noexcept;

// Returning a region is the transform shape, not this one.
OR_int_out f_region_return(producer_handle_int&&, OR_int_in&&) noexcept;

// Two regions is the binary transform shape, not this one.
void f_two_regions(OR_int_in&&, OR_int_in&&) noexcept;

void f_no_noexcept_producer(producer_handle_int_no_noexcept&&, OR_int_in&&) noexcept;
void f_const_push_in_producer_slot(producer_handle_const_push&&, OR_int_in&&) noexcept;
void f_rref_push_in_producer_slot(producer_handle_rref_push&&, OR_int_in&&) noexcept;

}  // namespace pe_test

namespace {

void test_runtime_smoke() { EXPECT_TRUE(extract::producer_endpoint_smoke_test()); }

void test_positive_well_formed() {
    static_assert(extract::ProducerEndpoint<&pe_test::f_well_formed>);
    static_assert(extract::is_producer_endpoint_v<&pe_test::f_well_formed>);
    static_assert(extract::ProducerEndpoint<&pe_test::f_well_formed_float>);
}

void test_positive_value_mismatch_admitted_by_concept() {
    static_assert(extract::ProducerEndpoint<&pe_test::f_value_mismatch>);
    static_assert(extract::is_producer_endpoint_v<&pe_test::f_value_mismatch>);
}

void test_negative_arity_mismatch() {
    static_assert(!extract::ProducerEndpoint<&pe_test::f_no_param>);
    static_assert(!extract::ProducerEndpoint<&pe_test::f_one_param>);
    static_assert(!extract::ProducerEndpoint<&pe_test::f_three_params>);
}

void test_negative_handle_not_rvalue_ref() { static_assert(!extract::ProducerEndpoint<&pe_test::f_handle_lvalue_ref>); }

void test_negative_handle_const_rvalue_ref() {
    static_assert(!extract::ProducerEndpoint<&pe_test::f_handle_const_rvalue_ref>);
}

void test_negative_region_not_rvalue_ref() { static_assert(!extract::ProducerEndpoint<&pe_test::f_region_lvalue_ref>); }

void test_negative_region_const_rvalue_ref() {
    static_assert(!extract::ProducerEndpoint<&pe_test::f_region_const_rvalue_ref>);
}

void test_negative_handle_slot_not_handle() {
    static_assert(!extract::ProducerEndpoint<&pe_test::f_int_in_handle_slot>);
    static_assert(!extract::ProducerEndpoint<&pe_test::f_ptr_to_handle>);
}

void test_negative_consumer_in_producer_slot() {
    static_assert(!extract::ProducerEndpoint<&pe_test::f_consumer_in_producer_slot>);
}

void test_negative_hybrid_in_producer_slot() {
    static_assert(!extract::ProducerEndpoint<&pe_test::f_hybrid_in_producer_slot>);
}

void test_negative_region_slot_not_region() {
    static_assert(!extract::ProducerEndpoint<&pe_test::f_int_in_region_slot>);
}

void test_negative_non_void_return() {
    static_assert(!extract::ProducerEndpoint<&pe_test::f_int_return>);
    static_assert(!extract::ProducerEndpoint<&pe_test::f_region_return>);
}

void test_negative_two_regions_is_binary() {
    static_assert(!extract::ProducerEndpoint<&pe_test::f_two_regions>);
    static_assert(extract::BinaryTransform<&pe_test::f_two_regions>);
}

void test_positive_non_noexcept_try_push() {
    // Every other test here uses a noexcept handle, so this is the only
    // one that reaches the throwing specialization.
    static_assert(extract::ProducerEndpoint<&pe_test::f_no_noexcept_producer>);
    static_assert(std::is_same_v<extract::producer_endpoint_handle_value_t<&pe_test::f_no_noexcept_producer>, int>);
}

void test_negative_cv_qualified_try_push() {
    static_assert(!extract::ProducerEndpoint<&pe_test::f_const_push_in_producer_slot>);

    static_assert(!extract::ProducerEndpoint<&pe_test::f_rref_push_in_producer_slot>);
}

void test_volatile_admitted_on_either_or_both() {
    static_assert(extract::ProducerEndpoint<&pe_test::f_handle_volatile>);
    static_assert(extract::ProducerEndpoint<&pe_test::f_region_volatile>);
    static_assert(extract::ProducerEndpoint<&pe_test::f_both_volatile>);
}

void test_handle_value_extraction() {
    static_assert(std::is_same_v<extract::producer_endpoint_handle_value_t<&pe_test::f_well_formed>, int>);
    static_assert(std::is_same_v<extract::producer_endpoint_handle_value_t<&pe_test::f_well_formed_float>, float>);
    // Each extractor reads its own parameter, so a mismatched pair must
    // yield the two different types rather than one of them twice.
    static_assert(std::is_same_v<extract::producer_endpoint_handle_value_t<&pe_test::f_value_mismatch>, int>);
}

void test_region_tag_extraction() {
    static_assert(std::is_same_v<extract::producer_endpoint_region_tag_t<&pe_test::f_well_formed>, in_tag>);
    static_assert(std::is_same_v<extract::producer_endpoint_region_tag_t<&pe_test::f_value_mismatch>, in_tag>);
    static_assert(std::is_same_v<extract::producer_endpoint_region_tag_t<&pe_test::f_well_formed_float>, in_tag>);
}

void test_region_value_extraction() {
    static_assert(std::is_same_v<extract::producer_endpoint_region_value_t<&pe_test::f_well_formed>, int>);
    static_assert(std::is_same_v<extract::producer_endpoint_region_value_t<&pe_test::f_well_formed_float>, float>);
    static_assert(std::is_same_v<extract::producer_endpoint_region_value_t<&pe_test::f_value_mismatch>, float>);
}

void test_value_consistency_predicate() {
    static_assert(extract::producer_endpoint_value_consistent_v<&pe_test::f_well_formed>);
    static_assert(extract::producer_endpoint_value_consistent_v<&pe_test::f_well_formed_float>);

    // Mismatch: handle int + region float → not consistent.
    static_assert(!extract::producer_endpoint_value_consistent_v<&pe_test::f_value_mismatch>);
}

void test_concept_form_in_constraints() {
    // The concept is used as a constraint, not only as a predicate, so
    // it is exercised in that position too.
    auto callable_with_endpoint = []<auto FnPtr>()
        requires extract::ProducerEndpoint<FnPtr>
    { return true; };

    EXPECT_TRUE(callable_with_endpoint.template operator()<&pe_test::f_well_formed>());
    EXPECT_TRUE(callable_with_endpoint.template operator()<&pe_test::f_well_formed_float>());
    EXPECT_TRUE(callable_with_endpoint.template operator()<&pe_test::f_value_mismatch>());
}

void test_cross_shape_exclusion_with_unary_and_binary() {
    // A function has at most one canonical shape, so routing follows
    // from the signature rather than from the order the router tries
    // its concepts in.
    static_assert(extract::ProducerEndpoint<&pe_test::f_well_formed>);
    static_assert(!extract::UnaryTransform<&pe_test::f_well_formed>);
    static_assert(!extract::BinaryTransform<&pe_test::f_well_formed>);

    static_assert(!extract::ProducerEndpoint<&pe_test::f_two_regions>);
    static_assert(!extract::UnaryTransform<&pe_test::f_two_regions>);
    static_assert(extract::BinaryTransform<&pe_test::f_two_regions>);

    // A signature can also match none of the shapes.
    static_assert(!extract::ProducerEndpoint<&pe_test::f_handle_lvalue_ref>);
    static_assert(!extract::BinaryTransform<&pe_test::f_handle_lvalue_ref>);
    static_assert(!extract::UnaryTransform<&pe_test::f_handle_lvalue_ref>);
}

void test_inferred_tags_harvests_region_tag_only() {
    // Permission tags are harvested from owned regions.  A handle is
    // not one, so this shape contributes exactly the region's tag and
    // the handle contributes nothing.
    namespace proto = ::crucible::safety::proto;

    using Expected = proto::PermSet<in_tag>;
    static_assert(proto::perm_set_equal_v<extract::inferred_permission_tags_t<&pe_test::f_well_formed>, Expected>);

    static_assert(extract::inferred_permission_tags_count_v<&pe_test::f_well_formed> == 1);
    static_assert(extract::function_has_tag_v<&pe_test::f_well_formed, in_tag>);
    static_assert(!extract::function_has_tag_v<&pe_test::f_well_formed, out_tag>);
}

void test_runtime_consistency() {
    volatile std::size_t const cap = 50;
    bool baseline_pos = extract::is_producer_endpoint_v<&pe_test::f_well_formed>;
    bool baseline_neg = !extract::is_producer_endpoint_v<&pe_test::f_one_param>;
    EXPECT_TRUE(baseline_pos);
    EXPECT_TRUE(baseline_neg);
    for (std::size_t i = 0; i < cap; ++i) {
        EXPECT_TRUE(baseline_pos == extract::is_producer_endpoint_v<&pe_test::f_well_formed>);
        EXPECT_TRUE(baseline_neg == !extract::is_producer_endpoint_v<&pe_test::f_one_param>);
        EXPECT_TRUE(extract::ProducerEndpoint<&pe_test::f_well_formed>);
        EXPECT_TRUE(!extract::ProducerEndpoint<&pe_test::f_no_param>);
    }
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_producer_endpoint:\n");
    run_test("test_runtime_smoke", test_runtime_smoke);
    run_test("test_positive_well_formed", test_positive_well_formed);
    run_test("test_positive_value_mismatch_admitted_by_concept", test_positive_value_mismatch_admitted_by_concept);
    run_test("test_negative_arity_mismatch", test_negative_arity_mismatch);
    run_test("test_negative_handle_not_rvalue_ref", test_negative_handle_not_rvalue_ref);
    run_test("test_negative_handle_const_rvalue_ref", test_negative_handle_const_rvalue_ref);
    run_test("test_negative_region_not_rvalue_ref", test_negative_region_not_rvalue_ref);
    run_test("test_negative_region_const_rvalue_ref", test_negative_region_const_rvalue_ref);
    run_test("test_negative_handle_slot_not_handle", test_negative_handle_slot_not_handle);
    run_test("test_negative_consumer_in_producer_slot", test_negative_consumer_in_producer_slot);
    run_test("test_negative_hybrid_in_producer_slot", test_negative_hybrid_in_producer_slot);
    run_test("test_negative_region_slot_not_region", test_negative_region_slot_not_region);
    run_test("test_negative_non_void_return", test_negative_non_void_return);
    run_test("test_negative_two_regions_is_binary", test_negative_two_regions_is_binary);
    run_test("test_positive_non_noexcept_try_push", test_positive_non_noexcept_try_push);
    run_test("test_negative_cv_qualified_try_push", test_negative_cv_qualified_try_push);
    run_test("test_volatile_admitted_on_either_or_both", test_volatile_admitted_on_either_or_both);
    run_test("test_handle_value_extraction", test_handle_value_extraction);
    run_test("test_region_tag_extraction", test_region_tag_extraction);
    run_test("test_region_value_extraction", test_region_value_extraction);
    run_test("test_value_consistency_predicate", test_value_consistency_predicate);
    run_test("test_concept_form_in_constraints", test_concept_form_in_constraints);
    run_test("test_cross_shape_exclusion_with_unary_and_binary", test_cross_shape_exclusion_with_unary_and_binary);
    run_test("test_inferred_tags_harvests_region_tag_only", test_inferred_tags_harvests_region_tag_only);
    run_test("test_runtime_consistency", test_runtime_consistency);
    std::fprintf(stderr, "\n%d passed, %d failed\n", total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
