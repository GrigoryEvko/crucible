// A header that ships its own static_asserts stays unverified under the
// project warning flags until some translation unit includes it.  This
// one pulls the transform-shape header through the test target's warning
// matrix and runs its inline smoke body.

#include <crucible/safety/UnaryTransform.h>

#include <crucible/safety/BinaryTransform.h>
#include <crucible/safety/InferredPermissionTags.h>
#include <crucible/safety/OwnedRegion.h>

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

template <typename T>
using OR_in = safety::OwnedRegion<T, in_tag>;
template <typename T>
using OR_out = safety::OwnedRegion<T, out_tag>;

}  // namespace

// The concepts read the signature and never call the function, so these
// are declared without bodies.  Taking the address of a declared function
// inside a static_assert is a constant expression and needs no
// definition to link.  That matters because the region type has no
// default constructor, so a body could not construct one anyway.

namespace ut_test {

using OR_in_f = ::crucible::safety::OwnedRegion<float, ::in_tag>;
using OR_in_i = ::crucible::safety::OwnedRegion<int, ::in_tag>;
using OR_out_i = ::crucible::safety::OwnedRegion<int, ::out_tag>;
using OR_out_d = ::crucible::safety::OwnedRegion<double, ::out_tag>;

void f_in_place(OR_in_f&&) noexcept;

OR_in_f f_same_tag(OR_in_f&&) noexcept;

OR_out_i f_different_tag(OR_in_f&&) noexcept;

OR_out_d f_different_element(OR_in_f&&) noexcept;

void f_lvalue_ref(OR_in_f&) noexcept;

void f_const_lvalue_ref(OR_in_f const&) noexcept;

void f_no_param() noexcept;

void f_two_params(OR_in_f&&, OR_in_i&&) noexcept;

void f_int_param(int) noexcept;

int f_int_return(OR_in_f&&) noexcept;

double f_int_int(int) noexcept;

void f_ptr_to_region(OR_in_f*) noexcept;

// A const rvalue reference is still an rvalue reference by the language
// rule, yet nothing can be moved out of a const value.  The concept must
// reject this shape.
void f_const_rvalue_ref(OR_in_f const&&) noexcept;

// Volatile is orthogonal to ownership transfer, so this shape is
// admitted.
void f_volatile_rvalue_ref(OR_in_f volatile&&) noexcept;

}  // namespace ut_test

// Member function pointers are out of scope.  Reflection on one throws
// rather than reporting a non-function, so it reaches the caller as a
// hard compile error instead of a concept that evaluates false.  Telling
// the two kinds of pointer apart belongs to the signature traits, and
// this concept inherits whatever they do.

namespace {

void test_runtime_smoke() { EXPECT_TRUE(extract::unary_transform_smoke_test()); }

void test_positive_in_place() {
    static_assert(extract::UnaryTransform<&ut_test::f_in_place>);
    static_assert(extract::is_unary_transform_v<&ut_test::f_in_place>);
}

void test_positive_same_tag_return() { static_assert(extract::UnaryTransform<&ut_test::f_same_tag>); }

void test_positive_different_tag_return() { static_assert(extract::UnaryTransform<&ut_test::f_different_tag>); }

void test_positive_different_element_type() { static_assert(extract::UnaryTransform<&ut_test::f_different_element>); }

void test_negative_lvalue_ref() {
    // This shape consumes ownership, so a borrow does not match.
    static_assert(!extract::UnaryTransform<&ut_test::f_lvalue_ref>);
    static_assert(!extract::UnaryTransform<&ut_test::f_const_lvalue_ref>);
}

void test_negative_arity_mismatch() {
    static_assert(!extract::UnaryTransform<&ut_test::f_no_param>);
    static_assert(!extract::UnaryTransform<&ut_test::f_two_params>);
}

void test_negative_non_region_parameter() {
    static_assert(!extract::UnaryTransform<&ut_test::f_int_param>);
    static_assert(!extract::UnaryTransform<&ut_test::f_int_int>);
}

void test_negative_non_region_return() { static_assert(!extract::UnaryTransform<&ut_test::f_int_return>); }

void test_negative_pointer_param() {
    // The region trait does not strip pointers, so a pointer to a
    // region is not one, and this concept inherits that.
    static_assert(!extract::UnaryTransform<&ut_test::f_ptr_to_region>);
}

void test_concept_form_in_constraints() {
    auto callable_with_unary = []<auto FnPtr>()
        requires extract::UnaryTransform<FnPtr>
    { return true; };

    EXPECT_TRUE(callable_with_unary.template operator()<&ut_test::f_in_place>());
    EXPECT_TRUE(callable_with_unary.template operator()<&ut_test::f_same_tag>());
    EXPECT_TRUE(callable_with_unary.template operator()<&ut_test::f_different_tag>());
}

void test_negative_const_rvalue_ref() { static_assert(!extract::UnaryTransform<&ut_test::f_const_rvalue_ref>); }

void test_volatile_rvalue_ref_admitted() { static_assert(extract::UnaryTransform<&ut_test::f_volatile_rvalue_ref>); }

void test_in_place_refinement() {
    // In place means a void return, and out of place means the
    // transform returns a region.
    static_assert(extract::is_in_place_unary_transform_v<&ut_test::f_in_place>);

    static_assert(!extract::is_in_place_unary_transform_v<&ut_test::f_same_tag>);
    static_assert(!extract::is_in_place_unary_transform_v<&ut_test::f_different_tag>);
    static_assert(!extract::is_in_place_unary_transform_v<&ut_test::f_different_element>);

    // A function of the wrong shape reports false here rather than
    // failing substitution.
    static_assert(!extract::is_in_place_unary_transform_v<&ut_test::f_int_param>);
}

void test_input_tag_extraction() {
    static_assert(std::is_same_v<extract::unary_transform_input_tag_t<&ut_test::f_in_place>, in_tag>);
    static_assert(std::is_same_v<extract::unary_transform_input_tag_t<&ut_test::f_same_tag>, in_tag>);
    static_assert(std::is_same_v<extract::unary_transform_input_tag_t<&ut_test::f_different_tag>, in_tag>);
}

void test_input_value_type_extraction() {
    static_assert(std::is_same_v<extract::unary_transform_input_value_t<&ut_test::f_in_place>, float>);
    static_assert(std::is_same_v<extract::unary_transform_input_value_t<&ut_test::f_different_element>, float>);
}

void test_output_tag_extraction() {
    // An in-place transform has no output region, so its output tag is
    // void.
    static_assert(std::is_same_v<extract::unary_transform_output_tag_t<&ut_test::f_in_place>, void>);

    static_assert(std::is_same_v<extract::unary_transform_output_tag_t<&ut_test::f_same_tag>, in_tag>);

    static_assert(std::is_same_v<extract::unary_transform_output_tag_t<&ut_test::f_different_tag>, out_tag>);

    static_assert(std::is_same_v<extract::unary_transform_output_tag_t<&ut_test::f_different_element>, out_tag>);
}

void test_runtime_consistency() {
    volatile std::size_t const cap = 50;
    bool baseline_pos = extract::is_unary_transform_v<&ut_test::f_in_place>;
    bool baseline_neg = !extract::is_unary_transform_v<&ut_test::f_int_param>;
    EXPECT_TRUE(baseline_pos);
    EXPECT_TRUE(baseline_neg);
    for (std::size_t i = 0; i < cap; ++i) {
        EXPECT_TRUE(baseline_pos == extract::is_unary_transform_v<&ut_test::f_in_place>);
        EXPECT_TRUE(baseline_neg == !extract::is_unary_transform_v<&ut_test::f_int_param>);
        EXPECT_TRUE(extract::UnaryTransform<&ut_test::f_same_tag>);
        EXPECT_TRUE(!extract::UnaryTransform<&ut_test::f_no_param>);
    }
}

void test_cross_shape_exclusion_with_binary() {
    // Arity separates the two shapes structurally, so no function may
    // match both.  Per-shape routing depends on each function having at
    // most one canonical shape.
    static_assert(extract::UnaryTransform<&ut_test::f_in_place>);
    static_assert(!extract::BinaryTransform<&ut_test::f_in_place>);

    static_assert(!extract::UnaryTransform<&ut_test::f_two_params>);
    static_assert(extract::BinaryTransform<&ut_test::f_two_params>);

    static_assert(extract::UnaryTransform<&ut_test::f_same_tag>);
    static_assert(!extract::BinaryTransform<&ut_test::f_same_tag>);
}

void test_cross_shape_exclusion_with_tag_free() {
    // The shape consumes a region, so it carries at least one inferred
    // tag and can never be tag-free.  A tag-free function has no
    // region parameter and so can never match the shape.
    static_assert(extract::UnaryTransform<&ut_test::f_in_place>);
    static_assert(!extract::is_tag_free_function_v<&ut_test::f_in_place>);

    static_assert(!extract::UnaryTransform<&ut_test::f_no_param>);
    static_assert(extract::is_tag_free_function_v<&ut_test::f_no_param>);
    static_assert(!extract::UnaryTransform<&ut_test::f_int_param>);
    static_assert(extract::is_tag_free_function_v<&ut_test::f_int_param>);

    static_assert(!extract::UnaryTransform<&ut_test::f_int_int>);
    static_assert(extract::is_tag_free_function_v<&ut_test::f_int_int>);
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_unary_transform:\n");
    run_test("test_runtime_smoke", test_runtime_smoke);
    run_test("test_positive_in_place", test_positive_in_place);
    run_test("test_positive_same_tag_return", test_positive_same_tag_return);
    run_test("test_positive_different_tag_return", test_positive_different_tag_return);
    run_test("test_positive_different_element_type", test_positive_different_element_type);
    run_test("test_negative_lvalue_ref", test_negative_lvalue_ref);
    run_test("test_negative_arity_mismatch", test_negative_arity_mismatch);
    run_test("test_negative_non_region_parameter", test_negative_non_region_parameter);
    run_test("test_negative_non_region_return", test_negative_non_region_return);
    run_test("test_negative_pointer_param", test_negative_pointer_param);
    run_test("test_concept_form_in_constraints", test_concept_form_in_constraints);
    run_test("test_negative_const_rvalue_ref", test_negative_const_rvalue_ref);
    run_test("test_volatile_rvalue_ref_admitted", test_volatile_rvalue_ref_admitted);
    run_test("test_in_place_refinement", test_in_place_refinement);
    run_test("test_input_tag_extraction", test_input_tag_extraction);
    run_test("test_input_value_type_extraction", test_input_value_type_extraction);
    run_test("test_output_tag_extraction", test_output_tag_extraction);
    run_test("test_cross_shape_exclusion_with_binary", test_cross_shape_exclusion_with_binary);
    run_test("test_cross_shape_exclusion_with_tag_free", test_cross_shape_exclusion_with_tag_free);
    run_test("test_runtime_consistency", test_runtime_consistency);
    std::fprintf(stderr, "\n%d passed, %d failed\n", total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
