// Including the header is itself part of the claim.  The static_asserts
// it carries are never compiled under the project warning flags until
// some translation unit pulls it in.

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

struct lhs_tag {};
struct rhs_tag {};
struct out_tag {};

}  // namespace

namespace bt_test {

using OR_lhs_f = ::crucible::safety::OwnedRegion<float, ::lhs_tag>;
using OR_lhs_d = ::crucible::safety::OwnedRegion<double, ::lhs_tag>;
using OR_rhs_i = ::crucible::safety::OwnedRegion<int, ::rhs_tag>;
using OR_rhs_f = ::crucible::safety::OwnedRegion<float, ::rhs_tag>;
using OR_out_d = ::crucible::safety::OwnedRegion<double, ::out_tag>;

void f_in_place_distinct(OR_lhs_f&&, OR_rhs_i&&) noexcept;
void f_in_place_same_tag(OR_lhs_f&&, OR_lhs_d&&) noexcept;

OR_out_d f_out_distinct(OR_lhs_f&&, OR_rhs_i&&) noexcept;
OR_lhs_d f_out_matches_lhs(OR_lhs_f&&, OR_rhs_i&&) noexcept;
OR_out_d f_different_elements(OR_lhs_f&&, OR_rhs_i&&) noexcept;

void f_no_param() noexcept;
void f_one_param(OR_lhs_f&&) noexcept;
void f_three_params(OR_lhs_f&&, OR_rhs_i&&, int) noexcept;

void f_lhs_lvalue_ref(OR_lhs_f&, OR_rhs_i&&) noexcept;
void f_rhs_lvalue_ref(OR_lhs_f&&, OR_rhs_i&) noexcept;

void f_lhs_const_rvalue_ref(OR_lhs_f const&&, OR_rhs_i&&) noexcept;
void f_rhs_const_rvalue_ref(OR_lhs_f&&, OR_rhs_i const&&) noexcept;

void f_lhs_int(int, OR_rhs_i&&) noexcept;
void f_rhs_int(OR_lhs_f&&, int) noexcept;

int f_int_return(OR_lhs_f&&, OR_rhs_i&&) noexcept;

void f_lhs_volatile_rvalue_ref(OR_lhs_f volatile&&, OR_rhs_i&&) noexcept;
void f_rhs_volatile_rvalue_ref(OR_lhs_f&&, OR_rhs_i volatile&&) noexcept;
void f_both_volatile_rvalue_ref(OR_lhs_f volatile&&, OR_rhs_i volatile&&) noexcept;

}  // namespace bt_test

namespace {

void test_runtime_smoke() { EXPECT_TRUE(extract::binary_transform_smoke_test()); }

void test_positive_in_place_distinct_tags() {
    static_assert(extract::BinaryTransform<&bt_test::f_in_place_distinct>);
    static_assert(extract::is_binary_transform_v<&bt_test::f_in_place_distinct>);
    static_assert(extract::is_in_place_binary_transform_v<&bt_test::f_in_place_distinct>);
}

void test_positive_in_place_same_tag() {
    // The concept is indifferent to whether the two input tags differ.
    static_assert(extract::BinaryTransform<&bt_test::f_in_place_same_tag>);
    static_assert(extract::is_in_place_binary_transform_v<&bt_test::f_in_place_same_tag>);
}

void test_positive_out_of_place_distinct() {
    static_assert(extract::BinaryTransform<&bt_test::f_out_distinct>);
    static_assert(!extract::is_in_place_binary_transform_v<&bt_test::f_out_distinct>);
}

void test_positive_out_of_place_matches_lhs() {
    static_assert(extract::BinaryTransform<&bt_test::f_out_matches_lhs>);
    static_assert(!extract::is_in_place_binary_transform_v<&bt_test::f_out_matches_lhs>);
}

void test_positive_different_element_types() {
    static_assert(extract::BinaryTransform<&bt_test::f_different_elements>);
}

void test_negative_arity_mismatch() {
    static_assert(!extract::BinaryTransform<&bt_test::f_no_param>);
    static_assert(!extract::BinaryTransform<&bt_test::f_one_param>);
    static_assert(!extract::BinaryTransform<&bt_test::f_three_params>);
}

void test_negative_lvalue_ref() {
    // An lvalue reference borrows the region instead of consuming it.
    static_assert(!extract::BinaryTransform<&bt_test::f_lhs_lvalue_ref>);
    static_assert(!extract::BinaryTransform<&bt_test::f_rhs_lvalue_ref>);
}

void test_negative_const_rvalue_ref() {
    // Nothing can be moved out of a const rvalue reference.
    static_assert(!extract::BinaryTransform<&bt_test::f_lhs_const_rvalue_ref>);
    static_assert(!extract::BinaryTransform<&bt_test::f_rhs_const_rvalue_ref>);
}

void test_negative_non_region_parameter() {
    static_assert(!extract::BinaryTransform<&bt_test::f_lhs_int>);
    static_assert(!extract::BinaryTransform<&bt_test::f_rhs_int>);
}

void test_negative_non_region_return() { static_assert(!extract::BinaryTransform<&bt_test::f_int_return>); }

void test_lhs_tag_extraction() {
    static_assert(std::is_same_v<extract::binary_transform_lhs_tag_t<&bt_test::f_in_place_distinct>, lhs_tag>);
    static_assert(std::is_same_v<extract::binary_transform_lhs_tag_t<&bt_test::f_in_place_same_tag>, lhs_tag>);
    static_assert(std::is_same_v<extract::binary_transform_lhs_tag_t<&bt_test::f_out_distinct>, lhs_tag>);
}

void test_rhs_tag_extraction() {
    static_assert(std::is_same_v<extract::binary_transform_rhs_tag_t<&bt_test::f_in_place_distinct>, rhs_tag>);

    static_assert(std::is_same_v<extract::binary_transform_rhs_tag_t<&bt_test::f_in_place_same_tag>, lhs_tag>);
}

void test_value_type_extraction() {
    static_assert(std::is_same_v<extract::binary_transform_lhs_value_t<&bt_test::f_in_place_distinct>, float>);
    static_assert(std::is_same_v<extract::binary_transform_rhs_value_t<&bt_test::f_in_place_distinct>, int>);
    static_assert(std::is_same_v<extract::binary_transform_lhs_value_t<&bt_test::f_in_place_same_tag>, float>);
    static_assert(std::is_same_v<extract::binary_transform_rhs_value_t<&bt_test::f_in_place_same_tag>, double>);
}

void test_output_tag_extraction() {
    static_assert(std::is_same_v<extract::binary_transform_output_tag_t<&bt_test::f_in_place_distinct>, void>);
    static_assert(std::is_same_v<extract::binary_transform_output_tag_t<&bt_test::f_in_place_same_tag>, void>);

    static_assert(std::is_same_v<extract::binary_transform_output_tag_t<&bt_test::f_out_distinct>, out_tag>);

    static_assert(std::is_same_v<extract::binary_transform_output_tag_t<&bt_test::f_out_matches_lhs>, lhs_tag>);
}

void test_concept_form_in_constraints() {
    auto callable_with_binary = []<auto FnPtr>()
        requires extract::BinaryTransform<FnPtr>
    { return true; };

    EXPECT_TRUE(callable_with_binary.template operator()<&bt_test::f_in_place_distinct>());
    EXPECT_TRUE(callable_with_binary.template operator()<&bt_test::f_out_distinct>());
}

void test_volatile_rvalue_ref_admitted() {
    // A volatile qualifier is orthogonal to ownership transfer, so the
    // concept still admits these three.
    static_assert(extract::BinaryTransform<&bt_test::f_lhs_volatile_rvalue_ref>);
    static_assert(extract::BinaryTransform<&bt_test::f_rhs_volatile_rvalue_ref>);
    static_assert(extract::BinaryTransform<&bt_test::f_both_volatile_rvalue_ref>);
}

void test_has_same_tag_predicate() {
    static_assert(!extract::binary_transform_has_same_tag_v<&bt_test::f_in_place_distinct>);
    static_assert(!extract::binary_transform_has_same_tag_v<&bt_test::f_out_distinct>);
    static_assert(!extract::binary_transform_has_same_tag_v<&bt_test::f_out_matches_lhs>);
    // The predicate reads the two inputs only.  The third case returns a
    // region tagged like its left input and is still counted distinct.

    static_assert(extract::binary_transform_has_same_tag_v<&bt_test::f_in_place_same_tag>);
}

void test_runtime_consistency() {
    volatile std::size_t const cap = 50;
    bool baseline_pos = extract::is_binary_transform_v<&bt_test::f_in_place_distinct>;
    bool baseline_neg = !extract::is_binary_transform_v<&bt_test::f_one_param>;
    EXPECT_TRUE(baseline_pos);
    EXPECT_TRUE(baseline_neg);
    for (std::size_t i = 0; i < cap; ++i) {
        EXPECT_TRUE(baseline_pos == extract::is_binary_transform_v<&bt_test::f_in_place_distinct>);
        EXPECT_TRUE(baseline_neg == !extract::is_binary_transform_v<&bt_test::f_one_param>);
        EXPECT_TRUE(extract::BinaryTransform<&bt_test::f_out_distinct>);
        EXPECT_TRUE(!extract::BinaryTransform<&bt_test::f_no_param>);
    }
}

void test_cross_shape_exclusion_with_unary() {
    // Dispatch routes on shape, so a function must carry at most one of
    // them.  The arity difference makes the two mutually exclusive.
    static_assert(extract::BinaryTransform<&bt_test::f_in_place_distinct>);
    static_assert(!extract::UnaryTransform<&bt_test::f_in_place_distinct>);

    // The one-parameter probe is a negative fixture above and a positive
    // one here.
    static_assert(!extract::BinaryTransform<&bt_test::f_one_param>);
    static_assert(extract::UnaryTransform<&bt_test::f_one_param>);

    static_assert(extract::BinaryTransform<&bt_test::f_out_distinct>);
    static_assert(!extract::UnaryTransform<&bt_test::f_out_distinct>);
}

void test_cross_shape_exclusion_with_tag_free() {
    // Consuming two regions means carrying at least one tag, so nothing
    // can be both a binary transform and tag-free.
    static_assert(!extract::is_tag_free_function_v<&bt_test::f_in_place_distinct>);
    static_assert(!extract::is_tag_free_function_v<&bt_test::f_in_place_same_tag>);
    static_assert(!extract::is_tag_free_function_v<&bt_test::f_out_distinct>);

    static_assert(!extract::BinaryTransform<&bt_test::f_no_param>);
    static_assert(extract::is_tag_free_function_v<&bt_test::f_no_param>);

    // The three-parameter probe fails both predicates.  Its arity rules
    // out the transform, and the tag harvest skips its plain int and
    // still finds two tags, which rules out tag-free.
    static_assert(!extract::BinaryTransform<&bt_test::f_three_params>);
    static_assert(!extract::is_tag_free_function_v<&bt_test::f_three_params>);
}

void test_inferred_tags_match_extracted_tags() {
    // Two surfaces report the tags a function carries: the harvested set
    // and the per-position extractors.  Automatic routing is only sound
    // while the two agree.
    namespace proto = ::crucible::safety::proto;

    using TagsExpected = proto::PermSet<lhs_tag, rhs_tag>;
    static_assert(
        proto::perm_set_equal_v<extract::inferred_permission_tags_t<&bt_test::f_in_place_distinct>, TagsExpected>);

    static_assert(extract::function_has_tag_v<&bt_test::f_in_place_distinct,
                                              extract::binary_transform_lhs_tag_t<&bt_test::f_in_place_distinct>>);
    static_assert(extract::function_has_tag_v<&bt_test::f_in_place_distinct,
                                              extract::binary_transform_rhs_tag_t<&bt_test::f_in_place_distinct>>);

    // Two inputs sharing one tag collapse the harvested set to a single
    // entry.
    static_assert(extract::inferred_permission_tags_count_v<&bt_test::f_in_place_same_tag> == 1);

    static_assert(extract::binary_transform_has_same_tag_v<&bt_test::f_in_place_same_tag>);
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_binary_transform:\n");
    run_test("test_runtime_smoke", test_runtime_smoke);
    run_test("test_positive_in_place_distinct_tags", test_positive_in_place_distinct_tags);
    run_test("test_positive_in_place_same_tag", test_positive_in_place_same_tag);
    run_test("test_positive_out_of_place_distinct", test_positive_out_of_place_distinct);
    run_test("test_positive_out_of_place_matches_lhs", test_positive_out_of_place_matches_lhs);
    run_test("test_positive_different_element_types", test_positive_different_element_types);
    run_test("test_negative_arity_mismatch", test_negative_arity_mismatch);
    run_test("test_negative_lvalue_ref", test_negative_lvalue_ref);
    run_test("test_negative_const_rvalue_ref", test_negative_const_rvalue_ref);
    run_test("test_negative_non_region_parameter", test_negative_non_region_parameter);
    run_test("test_negative_non_region_return", test_negative_non_region_return);
    run_test("test_lhs_tag_extraction", test_lhs_tag_extraction);
    run_test("test_rhs_tag_extraction", test_rhs_tag_extraction);
    run_test("test_value_type_extraction", test_value_type_extraction);
    run_test("test_output_tag_extraction", test_output_tag_extraction);
    run_test("test_concept_form_in_constraints", test_concept_form_in_constraints);
    run_test("test_volatile_rvalue_ref_admitted", test_volatile_rvalue_ref_admitted);
    run_test("test_has_same_tag_predicate", test_has_same_tag_predicate);
    run_test("test_cross_shape_exclusion_with_unary", test_cross_shape_exclusion_with_unary);
    run_test("test_cross_shape_exclusion_with_tag_free", test_cross_shape_exclusion_with_tag_free);
    run_test("test_inferred_tags_match_extracted_tags", test_inferred_tags_match_extracted_tags);
    run_test("test_runtime_consistency", test_runtime_consistency);
    std::fprintf(stderr, "\n%d passed, %d failed\n", total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
