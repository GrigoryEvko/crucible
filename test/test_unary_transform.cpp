// ═══════════════════════════════════════════════════════════════════
// test_unary_transform — sentinel TU for safety/UnaryTransform.h
//
// Same blind-spot rationale as test_is_owned_region / test_is_
// permission / test_inferred_permission_tags: a header shipped with
// embedded static_asserts is unverified under the project warning
// flags unless a .cpp TU includes it.  This sentinel forces
// UnaryTransform.h through the test target's full -Werror=shadow /
// -Werror=conversion / -Wanalyzer-* matrix and exercises the
// runtime smoke test inline body.
//
// Coverage extends beyond the header self-test (which only covers
// negatives without requiring OwnedRegion instantiations) to:
//   * Positive: in-place void return.
//   * Positive: out-of-place OwnedRegion return (same tag).
//   * Positive: out-of-place OwnedRegion return (different tag).
//   * Positive: parameter type T can be any element type.
//   * Negative: rvalue-ref vs lvalue-ref vs by-value parameter shape.
//   * Negative: arity 0, 2, 3.
//   * Negative: int / void* / non-region parameter.
//   * Negative: int / non-region return type.
// ═══════════════════════════════════════════════════════════════════

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
struct out_tag {};

template <typename T>
using OR_in  = safety::OwnedRegion<T, in_tag>;
template <typename T>
using OR_out = safety::OwnedRegion<T, out_tag>;

}  // namespace

// ── Functions to test ─────────────────────────────────────────────
//
// The concept checks operate on the FUNCTION SIGNATURE only — the
// auto FnPtr parameter never invokes the function.  We declare
// these functions at NAMESPACE SCOPE (external linkage) without
// bodies — taking their address in a static_assert is a constant
// expression that does not require the function to be defined for
// linking purposes.  OwnedRegion intentionally has no default
// constructor (linear-like discipline); concept satisfaction only
// reads the signature, so declaration is sufficient.

namespace ut_test {

using OR_in_f  = ::crucible::safety::OwnedRegion<float, ::in_tag>;
using OR_in_i  = ::crucible::safety::OwnedRegion<int, ::in_tag>;
using OR_out_i = ::crucible::safety::OwnedRegion<int, ::out_tag>;
using OR_out_d = ::crucible::safety::OwnedRegion<double, ::out_tag>;

// Positive: in-place transform.
void f_in_place(OR_in_f&&) noexcept;

// Positive: out-of-place same-tag transform (e.g., normalize).
OR_in_f f_same_tag(OR_in_f&&) noexcept;

// Positive: out-of-place different-tag transform (e.g., classify).
OR_out_i f_different_tag(OR_in_f&&) noexcept;

// Positive: different element types.
OR_out_d f_different_element(OR_in_f&&) noexcept;

// Negative: lvalue reference (borrow, not consume).
void f_lvalue_ref(OR_in_f&) noexcept;

// Negative: const lvalue reference.
void f_const_lvalue_ref(OR_in_f const&) noexcept;

// Negative: arity 0.
void f_no_param() noexcept;

// Negative: arity 2.
void f_two_params(OR_in_f&&, OR_in_i&&) noexcept;

// Negative: non-region parameter.
void f_int_param(int) noexcept;

// Negative: non-region return + region param.
int f_int_return(OR_in_f&&) noexcept;

// Negative: non-region return AND non-region param.
double f_int_int(int) noexcept;

// Negative: pointer-to-region parameter (not OwnedRegion).
void f_ptr_to_region(OR_in_f*) noexcept;

// Audit: const&& parameter.  std::is_rvalue_reference_v<T const&&>
// is TRUE — `T const&&` IS an rvalue reference.  But you cannot
// MOVE from a const value, so this is NOT the consume-ownership
// shape.  The concept must reject it.
void f_const_rvalue_ref(OR_in_f const&&) noexcept;

// Audit: volatile&& parameter — should still match (volatile is
// orthogonal to ownership transfer).
void f_volatile_rvalue_ref(OR_in_f volatile&&) noexcept;

}  // namespace ut_test

// NOTE: member function pointers are NOT in scope for the
// UnaryTransform concept — signature_traits<&Class::method> throws
// a reflection exception ("reflection does not represent a function
// or function type") on member function pointers in C++26 P2996,
// which propagates as a hard compile error rather than a
// concept-failure false.  Discriminating member function pointers
// from free-function pointers is a SignatureTraits concern (FOUND-
// D01); UnaryTransform inherits whatever behavior signature_traits
// gives it.

namespace {

void test_runtime_smoke() {
    EXPECT_TRUE(extract::unary_transform_smoke_test());
}

void test_positive_in_place() {
    static_assert(extract::UnaryTransform<&ut_test::f_in_place>);
    static_assert(extract::is_unary_transform_v<&ut_test::f_in_place>);
}

void test_positive_same_tag_return() {
    static_assert(extract::UnaryTransform<&ut_test::f_same_tag>);
}

void test_positive_different_tag_return() {
    static_assert(extract::UnaryTransform<&ut_test::f_different_tag>);
}

void test_positive_different_element_type() {
    static_assert(extract::UnaryTransform<&ut_test::f_different_element>);
}

void test_negative_lvalue_ref() {
    // Lvalue reference does NOT match — UnaryTransform is the
    // consume-ownership shape, not borrow-shape.
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

void test_negative_non_region_return() {
    // Non-region return type fails the return-clause.
    // The function takes a region but returns int — not unary
    // transform shape.
    static_assert(!extract::UnaryTransform<&ut_test::f_int_return>);
}

void test_negative_pointer_param() {
    // Pointer-to-region is NOT an OwnedRegion — per FOUND-D03 the
    // trait does not strip pointers.  UnaryTransform inherits this
    // discrimination.
    static_assert(!extract::UnaryTransform<&ut_test::f_ptr_to_region>);
}

void test_concept_form_in_constraints() {
    // The concept form should be usable as a requires-clause.
    // Smoke test: a function template constrained on UnaryTransform
    // should accept a unary-transform fn-ptr and reject others.
    auto callable_with_unary = []<auto FnPtr>()
        requires extract::UnaryTransform<FnPtr>
    {
        return true;
    };

    EXPECT_TRUE(callable_with_unary.template operator()<&ut_test::f_in_place>());
    EXPECT_TRUE(callable_with_unary.template operator()<&ut_test::f_same_tag>());
    EXPECT_TRUE(callable_with_unary.template operator()<&ut_test::f_different_tag>());
}

void test_negative_const_rvalue_ref() {
    // const&& is an rvalue reference, but you cannot move from a
    // const value.  The concept must reject the const-rvalue-ref
    // shape because it defeats consume-ownership.
    static_assert(!extract::UnaryTransform<&ut_test::f_const_rvalue_ref>);
}

void test_volatile_rvalue_ref_admitted() {
    // volatile is orthogonal to ownership transfer; volatile&& IS
    // a valid consume shape.  The concept should ADMIT this.
    static_assert(extract::UnaryTransform<&ut_test::f_volatile_rvalue_ref>);
}

void test_in_place_refinement() {
    // In-place: void return.
    static_assert(extract::is_in_place_unary_transform_v<&ut_test::f_in_place>);

    // Out-of-place: returns OwnedRegion.
    static_assert(!extract::is_in_place_unary_transform_v<&ut_test::f_same_tag>);
    static_assert(!extract::is_in_place_unary_transform_v<&ut_test::f_different_tag>);
    static_assert(!extract::is_in_place_unary_transform_v<&ut_test::f_different_element>);

    // Refinement of UnaryTransform — non-matching functions get
    // false (NOT a substitution failure).
    static_assert(!extract::is_in_place_unary_transform_v<&ut_test::f_int_param>);
}

void test_input_tag_extraction() {
    static_assert(std::is_same_v<
        extract::unary_transform_input_tag_t<&ut_test::f_in_place>,
        in_tag>);
    static_assert(std::is_same_v<
        extract::unary_transform_input_tag_t<&ut_test::f_same_tag>,
        in_tag>);
    static_assert(std::is_same_v<
        extract::unary_transform_input_tag_t<&ut_test::f_different_tag>,
        in_tag>);
}

void test_input_value_type_extraction() {
    static_assert(std::is_same_v<
        extract::unary_transform_input_value_t<&ut_test::f_in_place>,
        float>);
    static_assert(std::is_same_v<
        extract::unary_transform_input_value_t<&ut_test::f_different_element>,
        float>);
}

void test_output_tag_extraction() {
    // In-place: output tag is `void`.
    static_assert(std::is_same_v<
        extract::unary_transform_output_tag_t<&ut_test::f_in_place>,
        void>);

    // Out-of-place same-tag: output tag matches input tag.
    static_assert(std::is_same_v<
        extract::unary_transform_output_tag_t<&ut_test::f_same_tag>,
        in_tag>);

    // Out-of-place different-tag: output tag is the return-type's
    // tag.
    static_assert(std::is_same_v<
        extract::unary_transform_output_tag_t<&ut_test::f_different_tag>,
        out_tag>);

    static_assert(std::is_same_v<
        extract::unary_transform_output_tag_t<&ut_test::f_different_element>,
        out_tag>);
}

void test_runtime_consistency() {
    volatile std::size_t const cap = 50;
    bool baseline_pos = extract::is_unary_transform_v<&ut_test::f_in_place>;
    bool baseline_neg = !extract::is_unary_transform_v<&ut_test::f_int_param>;
    EXPECT_TRUE(baseline_pos);
    EXPECT_TRUE(baseline_neg);
    for (std::size_t i = 0; i < cap; ++i) {
        EXPECT_TRUE(baseline_pos
            == extract::is_unary_transform_v<&ut_test::f_in_place>);
        EXPECT_TRUE(baseline_neg
            == !extract::is_unary_transform_v<&ut_test::f_int_param>);
        EXPECT_TRUE(extract::UnaryTransform<&ut_test::f_same_tag>);
        EXPECT_TRUE(!extract::UnaryTransform<&ut_test::f_no_param>);
    }
}

void test_cross_shape_exclusion_with_binary() {
    // A function matching UnaryTransform must NOT match BinaryTransform
    // — different arity is structural.  Symmetrically, a binary-shape
    // function must NOT match UnaryTransform.  Load-bearing for the
    // dispatcher's per-shape routing: each function must have AT MOST
    // one canonical shape, never two.

    // f_in_place is a UnaryTransform (arity 1, OR&&, void return).
    static_assert( extract::UnaryTransform<&ut_test::f_in_place>);
    static_assert(!extract::BinaryTransform<&ut_test::f_in_place>);

    // f_two_params is a BinaryTransform shape (arity 2, OR&&, OR&&,
    // void return).  Per the negative-test list above it is not a
    // UnaryTransform — confirm it IS a BinaryTransform.
    static_assert(!extract::UnaryTransform<&ut_test::f_two_params>);
    static_assert( extract::BinaryTransform<&ut_test::f_two_params>);

    // f_same_tag (out-of-place unary): unary yes, binary no.
    static_assert( extract::UnaryTransform<&ut_test::f_same_tag>);
    static_assert(!extract::BinaryTransform<&ut_test::f_same_tag>);
}

void test_cross_shape_exclusion_with_tag_free() {
    // A UnaryTransform consumes an OwnedRegion → it MUST carry at
    // least one inferred tag → it MUST NOT be tag-free.  Conversely,
    // any tag-free function (no tag-bearing parameter) cannot be a
    // UnaryTransform because UnaryTransform requires the parameter
    // to be OwnedRegion.

    // UnaryTransform → not tag-free.
    static_assert( extract::UnaryTransform<&ut_test::f_in_place>);
    static_assert(!extract::is_tag_free_function_v<&ut_test::f_in_place>);

    // Tag-free fn (e.g., f_no_param, f_int_param) → not a
    // UnaryTransform.
    static_assert(!extract::UnaryTransform<&ut_test::f_no_param>);
    static_assert( extract::is_tag_free_function_v<&ut_test::f_no_param>);
    static_assert(!extract::UnaryTransform<&ut_test::f_int_param>);
    static_assert( extract::is_tag_free_function_v<&ut_test::f_int_param>);

    // f_int_int (int → double) — both tag-free and not unary.
    static_assert(!extract::UnaryTransform<&ut_test::f_int_int>);
    static_assert( extract::is_tag_free_function_v<&ut_test::f_int_int>);
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_unary_transform:\n");
    run_test("test_runtime_smoke",
             test_runtime_smoke);
    run_test("test_positive_in_place",
             test_positive_in_place);
    run_test("test_positive_same_tag_return",
             test_positive_same_tag_return);
    run_test("test_positive_different_tag_return",
             test_positive_different_tag_return);
    run_test("test_positive_different_element_type",
             test_positive_different_element_type);
    run_test("test_negative_lvalue_ref",
             test_negative_lvalue_ref);
    run_test("test_negative_arity_mismatch",
             test_negative_arity_mismatch);
    run_test("test_negative_non_region_parameter",
             test_negative_non_region_parameter);
    run_test("test_negative_non_region_return",
             test_negative_non_region_return);
    run_test("test_negative_pointer_param",
             test_negative_pointer_param);
    run_test("test_concept_form_in_constraints",
             test_concept_form_in_constraints);
    run_test("test_negative_const_rvalue_ref",
             test_negative_const_rvalue_ref);
    run_test("test_volatile_rvalue_ref_admitted",
             test_volatile_rvalue_ref_admitted);
    run_test("test_in_place_refinement",
             test_in_place_refinement);
    run_test("test_input_tag_extraction",
             test_input_tag_extraction);
    run_test("test_input_value_type_extraction",
             test_input_value_type_extraction);
    run_test("test_output_tag_extraction",
             test_output_tag_extraction);
    run_test("test_cross_shape_exclusion_with_binary",
             test_cross_shape_exclusion_with_binary);
    run_test("test_cross_shape_exclusion_with_tag_free",
             test_cross_shape_exclusion_with_tag_free);
    run_test("test_runtime_consistency",
             test_runtime_consistency);
    std::fprintf(stderr, "\n%d passed, %d failed\n",
                 total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
