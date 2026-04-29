// ═══════════════════════════════════════════════════════════════════
// test_binary_transform — sentinel TU for safety/BinaryTransform.h
//
// Same blind-spot rationale as test_unary_transform: forces
// BinaryTransform.h through full -Werror=* matrix and exercises the
// runtime smoke test.
//
// Coverage (broader than header self-test):
//   * Positive: in-place void return, two regions, distinct tags.
//   * Positive: same tag, both inputs (e.g., merge two slices).
//   * Positive: out-of-place — different output tag.
//   * Positive: out-of-place — output tag matches one of inputs.
//   * Positive: different element types across the two inputs.
//   * Negative: arity 0, 1, 3.
//   * Negative: lhs lvalue ref (borrow first input).
//   * Negative: rhs lvalue ref (borrow second input).
//   * Negative: lhs const&&.
//   * Negative: rhs const&&.
//   * Negative: lhs is non-region.
//   * Negative: rhs is non-region.
//   * Negative: non-region non-void return type.
//   * Refinement: in-place vs out-of-place predicate.
//   * Extraction: lhs_tag / rhs_tag / lhs_value / rhs_value /
//                 output_tag work across all worked examples.
// ═══════════════════════════════════════════════════════════════════

#include <crucible/safety/BinaryTransform.h>

#include <crucible/safety/InferredPermissionTags.h>
#include <crucible/safety/OwnedRegion.h>
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

// ── Positive shapes ─────────────────────────────────────────────

// In-place: distinct tags.
void f_in_place_distinct(OR_lhs_f&&, OR_rhs_i&&) noexcept;

// In-place: same tag for both inputs (e.g., two slices of the
// same region; D11 dedup will collapse to one tag).
void f_in_place_same_tag(OR_lhs_f&&, OR_lhs_d&&) noexcept;

// Out-of-place: distinct tags, output tag is third.
OR_out_d f_out_distinct(OR_lhs_f&&, OR_rhs_i&&) noexcept;

// Out-of-place: output tag matches lhs_tag.
OR_lhs_d f_out_matches_lhs(OR_lhs_f&&, OR_rhs_i&&) noexcept;

// Out-of-place: different element types across inputs.
OR_out_d f_different_elements(OR_lhs_f&&, OR_rhs_i&&) noexcept;

// ── Negative shapes ─────────────────────────────────────────────

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

// Volatile&& on either parameter — should still match (volatile is
// orthogonal to ownership transfer).
void f_lhs_volatile_rvalue_ref(OR_lhs_f volatile&&, OR_rhs_i&&) noexcept;
void f_rhs_volatile_rvalue_ref(OR_lhs_f&&, OR_rhs_i volatile&&) noexcept;
void f_both_volatile_rvalue_ref(OR_lhs_f volatile&&, OR_rhs_i volatile&&) noexcept;

}  // namespace bt_test

namespace {

void test_runtime_smoke() {
    EXPECT_TRUE(extract::binary_transform_smoke_test());
}

void test_positive_in_place_distinct_tags() {
    static_assert(extract::BinaryTransform<&bt_test::f_in_place_distinct>);
    static_assert(extract::is_binary_transform_v<&bt_test::f_in_place_distinct>);
    static_assert(extract::is_in_place_binary_transform_v<&bt_test::f_in_place_distinct>);
}

void test_positive_in_place_same_tag() {
    // Both inputs share lhs_tag — D11 will dedup to a single-tag set.
    // The CONCEPT does not care about tag distinctness; both
    // configurations are valid binary transforms.
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
    // Lhs OR Rhs lvalue ref → not consume-shape.
    static_assert(!extract::BinaryTransform<&bt_test::f_lhs_lvalue_ref>);
    static_assert(!extract::BinaryTransform<&bt_test::f_rhs_lvalue_ref>);
}

void test_negative_const_rvalue_ref() {
    // Either parameter being const&& → cannot move from const.
    static_assert(!extract::BinaryTransform<&bt_test::f_lhs_const_rvalue_ref>);
    static_assert(!extract::BinaryTransform<&bt_test::f_rhs_const_rvalue_ref>);
}

void test_negative_non_region_parameter() {
    static_assert(!extract::BinaryTransform<&bt_test::f_lhs_int>);
    static_assert(!extract::BinaryTransform<&bt_test::f_rhs_int>);
}

void test_negative_non_region_return() {
    static_assert(!extract::BinaryTransform<&bt_test::f_int_return>);
}

void test_lhs_tag_extraction() {
    static_assert(std::is_same_v<
        extract::binary_transform_lhs_tag_t<&bt_test::f_in_place_distinct>,
        lhs_tag>);
    static_assert(std::is_same_v<
        extract::binary_transform_lhs_tag_t<&bt_test::f_in_place_same_tag>,
        lhs_tag>);
    static_assert(std::is_same_v<
        extract::binary_transform_lhs_tag_t<&bt_test::f_out_distinct>,
        lhs_tag>);
}

void test_rhs_tag_extraction() {
    // Distinct tags: rhs_tag.
    static_assert(std::is_same_v<
        extract::binary_transform_rhs_tag_t<&bt_test::f_in_place_distinct>,
        rhs_tag>);

    // Same tag: BOTH lhs_tag.
    static_assert(std::is_same_v<
        extract::binary_transform_rhs_tag_t<&bt_test::f_in_place_same_tag>,
        lhs_tag>);
}

void test_value_type_extraction() {
    static_assert(std::is_same_v<
        extract::binary_transform_lhs_value_t<&bt_test::f_in_place_distinct>,
        float>);
    static_assert(std::is_same_v<
        extract::binary_transform_rhs_value_t<&bt_test::f_in_place_distinct>,
        int>);
    static_assert(std::is_same_v<
        extract::binary_transform_lhs_value_t<&bt_test::f_in_place_same_tag>,
        float>);
    static_assert(std::is_same_v<
        extract::binary_transform_rhs_value_t<&bt_test::f_in_place_same_tag>,
        double>);
}

void test_output_tag_extraction() {
    // In-place: output is `void`.
    static_assert(std::is_same_v<
        extract::binary_transform_output_tag_t<&bt_test::f_in_place_distinct>,
        void>);
    static_assert(std::is_same_v<
        extract::binary_transform_output_tag_t<&bt_test::f_in_place_same_tag>,
        void>);

    // Out-of-place distinct: output is out_tag.
    static_assert(std::is_same_v<
        extract::binary_transform_output_tag_t<&bt_test::f_out_distinct>,
        out_tag>);

    // Out-of-place matches lhs: output is lhs_tag.
    static_assert(std::is_same_v<
        extract::binary_transform_output_tag_t<&bt_test::f_out_matches_lhs>,
        lhs_tag>);
}

void test_concept_form_in_constraints() {
    auto callable_with_binary = []<auto FnPtr>()
        requires extract::BinaryTransform<FnPtr>
    {
        return true;
    };

    EXPECT_TRUE(callable_with_binary.template operator()<
        &bt_test::f_in_place_distinct>());
    EXPECT_TRUE(callable_with_binary.template operator()<
        &bt_test::f_out_distinct>());
}

void test_volatile_rvalue_ref_admitted() {
    // Volatile&& on lhs / rhs / both — concept must admit because
    // volatile is orthogonal to ownership transfer.  Same as D12
    // audit-pass test.
    static_assert(extract::BinaryTransform<&bt_test::f_lhs_volatile_rvalue_ref>);
    static_assert(extract::BinaryTransform<&bt_test::f_rhs_volatile_rvalue_ref>);
    static_assert(extract::BinaryTransform<&bt_test::f_both_volatile_rvalue_ref>);
}

void test_has_same_tag_predicate() {
    // f_in_place_distinct uses (lhs_tag, rhs_tag) — distinct.
    static_assert(!extract::binary_transform_has_same_tag_v<
        &bt_test::f_in_place_distinct>);
    static_assert(!extract::binary_transform_has_same_tag_v<
        &bt_test::f_out_distinct>);
    static_assert(!extract::binary_transform_has_same_tag_v<
        &bt_test::f_out_matches_lhs>);
    // (f_out_matches_lhs returns OR<lhs_tag> but inputs are still
    // (lhs_tag, rhs_tag) — same-tag is about the INPUTS only.)

    // f_in_place_same_tag uses (lhs_tag, lhs_tag) — same.
    static_assert(extract::binary_transform_has_same_tag_v<
        &bt_test::f_in_place_same_tag>);
}

void test_runtime_consistency() {
    volatile std::size_t const cap = 50;
    bool baseline_pos =
        extract::is_binary_transform_v<&bt_test::f_in_place_distinct>;
    bool baseline_neg =
        !extract::is_binary_transform_v<&bt_test::f_one_param>;
    EXPECT_TRUE(baseline_pos);
    EXPECT_TRUE(baseline_neg);
    for (std::size_t i = 0; i < cap; ++i) {
        EXPECT_TRUE(baseline_pos
            == extract::is_binary_transform_v<&bt_test::f_in_place_distinct>);
        EXPECT_TRUE(baseline_neg
            == !extract::is_binary_transform_v<&bt_test::f_one_param>);
        EXPECT_TRUE(extract::BinaryTransform<&bt_test::f_out_distinct>);
        EXPECT_TRUE(!extract::BinaryTransform<&bt_test::f_no_param>);
    }
}

void test_cross_shape_exclusion_with_unary() {
    // A function matching BinaryTransform must NOT match
    // UnaryTransform — arity 2 != arity 1, structural exclusion.
    // Symmetrically, a unary-shape function must NOT match
    // BinaryTransform.  Load-bearing for dispatcher per-shape
    // routing: each function has AT MOST one canonical shape.

    // f_in_place_distinct is BinaryTransform (arity 2, two OR&&,
    // void return).
    static_assert( extract::BinaryTransform<&bt_test::f_in_place_distinct>);
    static_assert(!extract::UnaryTransform<&bt_test::f_in_place_distinct>);

    // f_one_param is the unary shape (arity 1, OR&&, void return).
    // Per the negative-test list above it is not a BinaryTransform —
    // confirm it IS a UnaryTransform.
    static_assert(!extract::BinaryTransform<&bt_test::f_one_param>);
    static_assert( extract::UnaryTransform<&bt_test::f_one_param>);

    // Out-of-place binary: binary yes, unary no.
    static_assert( extract::BinaryTransform<&bt_test::f_out_distinct>);
    static_assert(!extract::UnaryTransform<&bt_test::f_out_distinct>);
}

void test_cross_shape_exclusion_with_tag_free() {
    // A BinaryTransform consumes two OwnedRegions → it carries at
    // LEAST one tag (one if same-tag, two if distinct) → it MUST
    // NOT be tag-free.  Tag-free functions cannot be BinaryTransforms.

    // BinaryTransform → not tag-free.
    static_assert(!extract::is_tag_free_function_v<
        &bt_test::f_in_place_distinct>);
    static_assert(!extract::is_tag_free_function_v<
        &bt_test::f_in_place_same_tag>);
    static_assert(!extract::is_tag_free_function_v<
        &bt_test::f_out_distinct>);

    // Tag-free fns (no_param, lhs_int+rhs_lvalue, etc.) → not
    // BinaryTransform.
    static_assert(!extract::BinaryTransform<&bt_test::f_no_param>);
    static_assert( extract::is_tag_free_function_v<&bt_test::f_no_param>);

    // f_three_params has int as third — but ALSO has two OR&&.  D11's
    // tag-harvest skips int and emits {lhs_tag, rhs_tag}, so it is
    // NOT tag-free.  And f_three_params has arity 3 → not Binary.
    static_assert(!extract::BinaryTransform<&bt_test::f_three_params>);
    static_assert(!extract::is_tag_free_function_v<&bt_test::f_three_params>);
}

void test_inferred_tags_match_extracted_tags() {
    // The dispatcher's auto-routing depends on the inferred-tag set
    // matching the per-extractor tag claims.  For BinaryTransform:
    //  - inferred_permission_tags_t<&fn> contains {lhs_tag, rhs_tag}
    //    (deduped to one if same-tag).
    //  - binary_transform_lhs_tag_t / rhs_tag_t expose the same tags.
    //
    // The two reading surfaces MUST agree.  This test pins the
    // semantic round-trip.

    namespace proto = ::crucible::safety::proto;

    // Distinct-tag binary: inferred set = {lhs_tag, rhs_tag}.
    using TagsExpected = proto::PermSet<lhs_tag, rhs_tag>;
    static_assert(proto::perm_set_equal_v<
        extract::inferred_permission_tags_t<&bt_test::f_in_place_distinct>,
        TagsExpected>);

    // The extracted tags match the harvested set membership.
    static_assert(extract::function_has_tag_v<
        &bt_test::f_in_place_distinct,
        extract::binary_transform_lhs_tag_t<&bt_test::f_in_place_distinct>>);
    static_assert(extract::function_has_tag_v<
        &bt_test::f_in_place_distinct,
        extract::binary_transform_rhs_tag_t<&bt_test::f_in_place_distinct>>);

    // Same-tag binary: inferred set collapses to {lhs_tag} (size 1).
    static_assert(extract::inferred_permission_tags_count_v<
        &bt_test::f_in_place_same_tag> == 1);

    // has_same_tag_v agrees with the count == 1 claim.
    static_assert(extract::binary_transform_has_same_tag_v<
        &bt_test::f_in_place_same_tag>);
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_binary_transform:\n");
    run_test("test_runtime_smoke",
             test_runtime_smoke);
    run_test("test_positive_in_place_distinct_tags",
             test_positive_in_place_distinct_tags);
    run_test("test_positive_in_place_same_tag",
             test_positive_in_place_same_tag);
    run_test("test_positive_out_of_place_distinct",
             test_positive_out_of_place_distinct);
    run_test("test_positive_out_of_place_matches_lhs",
             test_positive_out_of_place_matches_lhs);
    run_test("test_positive_different_element_types",
             test_positive_different_element_types);
    run_test("test_negative_arity_mismatch",
             test_negative_arity_mismatch);
    run_test("test_negative_lvalue_ref",
             test_negative_lvalue_ref);
    run_test("test_negative_const_rvalue_ref",
             test_negative_const_rvalue_ref);
    run_test("test_negative_non_region_parameter",
             test_negative_non_region_parameter);
    run_test("test_negative_non_region_return",
             test_negative_non_region_return);
    run_test("test_lhs_tag_extraction",
             test_lhs_tag_extraction);
    run_test("test_rhs_tag_extraction",
             test_rhs_tag_extraction);
    run_test("test_value_type_extraction",
             test_value_type_extraction);
    run_test("test_output_tag_extraction",
             test_output_tag_extraction);
    run_test("test_concept_form_in_constraints",
             test_concept_form_in_constraints);
    run_test("test_volatile_rvalue_ref_admitted",
             test_volatile_rvalue_ref_admitted);
    run_test("test_has_same_tag_predicate",
             test_has_same_tag_predicate);
    run_test("test_cross_shape_exclusion_with_unary",
             test_cross_shape_exclusion_with_unary);
    run_test("test_cross_shape_exclusion_with_tag_free",
             test_cross_shape_exclusion_with_tag_free);
    run_test("test_inferred_tags_match_extracted_tags",
             test_inferred_tags_match_extracted_tags);
    run_test("test_runtime_consistency",
             test_runtime_consistency);
    std::fprintf(stderr, "\n%d passed, %d failed\n",
                 total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
