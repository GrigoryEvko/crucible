// A header that only ever reaches the compiler through other headers is
// never built under the project warning matrix, and the static_asserts it
// embeds are never instantiated.  This translation unit exists to pull
// the reduction concept into a real compilation, drive its runtime smoke
// test, and pin the negative shapes the concept has to keep rejecting.

#include <crucible/safety/Reduction.h>

#include <crucible/safety/BinaryTransform.h>
#include <crucible/safety/ConsumerEndpoint.h>
#include <crucible/safety/InferredPermissionTags.h>
#include <crucible/safety/_OwnedRegion.h>
#include <crucible/safety/_PipelineStage.h>
#include <crucible/safety/ProducerEndpoint.h>
#include <crucible/safety/SwmrReader.h>
#include <crucible/safety/SwmrWriter.h>
#include <crucible/safety/UnaryTransform.h>
#include <crucible/safety/reduce_into.h>

#include <array>
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

struct input_tag {};
struct other_tag {};

struct PlusOp {
    constexpr int operator()(int const& a, int const& b) const noexcept { return a + b; }
};

struct MaxOp {
    constexpr int operator()(int const& a, int const& b) const noexcept { return a > b ? a : b; }
};

using HistArray = std::array<int, 256>;

struct HistOp {
    constexpr HistArray operator()(HistArray const& a, HistArray const& b) const noexcept {
        HistArray out{};
        for (std::size_t i = 0; i < a.size(); ++i) {
            out[i] = a[i] + b[i];
        }
        return out;
    }
};

}  // namespace

namespace red_test {

using OR_int_input = ::crucible::safety::OwnedRegion<int, ::input_tag>;
using OR_float_input = ::crucible::safety::OwnedRegion<float, ::input_tag>;
using OR_int_other = ::crucible::safety::OwnedRegion<int, ::other_tag>;

using RI_int_plus = ::crucible::safety::reduce_into<int, ::PlusOp>;
using RI_int_max = ::crucible::safety::reduce_into<int, ::MaxOp>;
using RI_hist = ::crucible::safety::reduce_into<::HistArray, ::HistOp>;

void f_sum_into(OR_int_input&&, RI_int_plus&) noexcept;
void f_max_into(OR_int_input&&, RI_int_max&) noexcept;
void f_hist_into(OR_int_input&&, RI_hist&) noexcept;
void f_other_tag(OR_int_other&&, RI_int_plus&) noexcept;
void f_float_input(OR_float_input&&, RI_int_plus&) noexcept;

void f_volatile_input(OR_int_input volatile&&, RI_int_plus&) noexcept;
void f_volatile_accumulator(OR_int_input&&, RI_int_plus volatile&) noexcept;

void f_no_param() noexcept;
void f_one_param(OR_int_input&&) noexcept;
void f_three_params(OR_int_input&&, RI_int_plus&, int) noexcept;

// An rvalue-ref accumulator consumes the partial state, which defeats
// iterative refinement: the caller needs it to outlive the call.
void f_accumulator_rvalue(OR_int_input&&, RI_int_plus&&) noexcept;

// A const accumulator cannot be mutated, so the reducer makes no
// progress.
void f_accumulator_const_ref(OR_int_input&&, RI_int_plus const&) noexcept;

// An lvalue-ref input is the borrow shape rather than the consume shape,
// and lowers through a different per-shape path.
void f_input_lvalue(OR_int_input&, RI_int_plus&) noexcept;

// Nothing can be moved out of a const rvalue ref.
void f_input_const_rvalue(OR_int_input const&&, RI_int_plus&) noexcept;

// Permission generation in the dispatcher hard-codes parameter 0 as the
// consumed input, so swapping the two is a structural mismatch rather
// than a stylistic one.
void f_swapped_order(RI_int_plus&, OR_int_input&&) noexcept;

// The reduced result already lives in the borrowed accumulator, so a
// return value would be a second, ambiguous copy of it.
int f_int_return(OR_int_input&&, RI_int_plus&) noexcept;

void f_int_first(int, RI_int_plus&) noexcept;
void f_non_reduce_into(OR_int_input&&, int&) noexcept;

void f_throws(OR_int_input&&, RI_int_plus&);

}  // namespace red_test

namespace {

void test_runtime_smoke() { EXPECT_TRUE(extract::reduction_smoke_test()); }

void test_positive_sum_into() {
    static_assert(extract::Reduction<&red_test::f_sum_into>);
    static_assert(extract::is_reduction_v<&red_test::f_sum_into>);
}

void test_positive_max_into() { static_assert(extract::Reduction<&red_test::f_max_into>); }

void test_positive_hist_into() { static_assert(extract::Reduction<&red_test::f_hist_into>); }

void test_positive_distinct_tag() { static_assert(extract::Reduction<&red_test::f_other_tag>); }

void test_positive_distinct_input_element() { static_assert(extract::Reduction<&red_test::f_float_input>); }

void test_volatile_qualified_admitted() {
    // A volatile qualifier does not change the reference category, which
    // is the only thing the concept reads to tell consume from borrow.
    static_assert(extract::Reduction<&red_test::f_volatile_input>);
    static_assert(extract::Reduction<&red_test::f_volatile_accumulator>);
}

void test_negative_arity_mismatch() {
    static_assert(!extract::Reduction<&red_test::f_no_param>);
    static_assert(!extract::Reduction<&red_test::f_one_param>);
    static_assert(!extract::Reduction<&red_test::f_three_params>);
}

void test_negative_accumulator_rvalue_ref() { static_assert(!extract::Reduction<&red_test::f_accumulator_rvalue>); }

void test_negative_accumulator_const_ref() { static_assert(!extract::Reduction<&red_test::f_accumulator_const_ref>); }

void test_negative_input_lvalue_ref() { static_assert(!extract::Reduction<&red_test::f_input_lvalue>); }

void test_negative_input_const_rvalue() { static_assert(!extract::Reduction<&red_test::f_input_const_rvalue>); }

void test_negative_swapped_order() { static_assert(!extract::Reduction<&red_test::f_swapped_order>); }

void test_negative_int_return() { static_assert(!extract::Reduction<&red_test::f_int_return>); }

void test_negative_int_first() { static_assert(!extract::Reduction<&red_test::f_int_first>); }

void test_negative_non_reduce_into_second() { static_assert(!extract::Reduction<&red_test::f_non_reduce_into>); }

void test_input_tag_extraction() {
    static_assert(std::is_same_v<extract::reduction_input_tag_t<&red_test::f_sum_into>, input_tag>);
    static_assert(std::is_same_v<extract::reduction_input_tag_t<&red_test::f_other_tag>, other_tag>);
}

void test_input_value_extraction() {
    static_assert(std::is_same_v<extract::reduction_input_value_t<&red_test::f_sum_into>, int>);
    static_assert(std::is_same_v<extract::reduction_input_value_t<&red_test::f_float_input>, float>);
}

void test_accumulator_extraction() {
    static_assert(std::is_same_v<extract::reduction_accumulator_t<&red_test::f_sum_into>, int>);
    static_assert(std::is_same_v<extract::reduction_accumulator_t<&red_test::f_hist_into>, HistArray>);
    EXPECT_TRUE((!std::is_same_v<extract::reduction_accumulator_t<&red_test::f_sum_into>,
                                 extract::reduction_accumulator_t<&red_test::f_hist_into>>));
}

void test_reducer_extraction() {
    static_assert(std::is_same_v<extract::reduction_reducer_t<&red_test::f_sum_into>, PlusOp>);
    static_assert(std::is_same_v<extract::reduction_reducer_t<&red_test::f_max_into>, MaxOp>);
    static_assert(std::is_same_v<extract::reduction_reducer_t<&red_test::f_hist_into>, HistOp>);
    EXPECT_TRUE((!std::is_same_v<extract::reduction_reducer_t<&red_test::f_sum_into>,
                                 extract::reduction_reducer_t<&red_test::f_max_into>>));
}

void test_concept_form_in_constraints() {
    auto callable_with_reduction = []<auto FnPtr>()
        requires extract::Reduction<FnPtr>
    { return true; };

    EXPECT_TRUE(callable_with_reduction.template operator()<&red_test::f_sum_into>());
    EXPECT_TRUE(callable_with_reduction.template operator()<&red_test::f_hist_into>());
}

void test_cross_shape_exclusion_with_unary() {
    static_assert(extract::Reduction<&red_test::f_sum_into>);
    static_assert(!extract::UnaryTransform<&red_test::f_sum_into>);

    static_assert(!extract::Reduction<&red_test::f_one_param>);
    static_assert(extract::UnaryTransform<&red_test::f_one_param>);
}

void test_cross_shape_exclusion_with_binary() {
    static_assert(extract::Reduction<&red_test::f_sum_into>);
    static_assert(!extract::BinaryTransform<&red_test::f_sum_into>);
}

// The dispatcher relies on every function having at most one canonical
// shape, so that routing is determined by the signature and not by the
// order in which the router tries its concepts.  The checks below look
// redundant against the pairwise ones above, and they are, until someone
// loosens one concept's wrapper detection: a Reduction-shaped function
// that then satisfies two concepts at once routes to whichever the
// router reaches first, and the call site shows nothing.

void test_cross_shape_exclusion_with_producer_endpoint() {
    // Parameter 0 is an OwnedRegion, not a producer handle.
    static_assert(extract::Reduction<&red_test::f_sum_into>);
    static_assert(!extract::ProducerEndpoint<&red_test::f_sum_into>);
    static_assert(!extract::ProducerEndpoint<&red_test::f_hist_into>);
}

void test_cross_shape_exclusion_with_consumer_endpoint() {
    // Parameter 0 is an OwnedRegion, not a consumer handle.
    static_assert(extract::Reduction<&red_test::f_sum_into>);
    static_assert(!extract::ConsumerEndpoint<&red_test::f_sum_into>);
    static_assert(!extract::ConsumerEndpoint<&red_test::f_max_into>);
}

void test_cross_shape_exclusion_with_swmr_writer() {
    // Two independent mismatches: parameter 0 is not a writer handle,
    // and parameter 1 is a reference where this shape needs a value.
    static_assert(extract::Reduction<&red_test::f_sum_into>);
    static_assert(!extract::SwmrWriter<&red_test::f_sum_into>);
}

void test_cross_shape_exclusion_with_swmr_reader() {
    // Two independent mismatches: arity 2 against 1, and a void return
    // against a value return.  Either alone excludes, so the exclusion
    // survives one of them being loosened.
    static_assert(extract::Reduction<&red_test::f_sum_into>);
    static_assert(!extract::SwmrReader<&red_test::f_sum_into>);
}

void test_cross_shape_exclusion_with_pipeline_stage() {
    // Neither parameter is a channel handle.
    static_assert(extract::Reduction<&red_test::f_sum_into>);
    static_assert(!extract::PipelineStage<&red_test::f_sum_into>);
    static_assert(!extract::PipelineStage<&red_test::f_hist_into>);
}

// The concept deliberately does not read the noexcept specifier.  The
// project builds with -fno-exceptions, so noexcept carries no
// type-system meaning here and the dispatcher routes both forms the
// same way.  Pinning the admission stops a later noexcept fence from
// quietly excluding legitimate signatures.

void test_noexcept_false_admitted() {
    static_assert(extract::Reduction<&red_test::f_throws>);
    static_assert(std::is_same_v<extract::reduction_input_tag_t<&red_test::f_throws>, input_tag>);
    static_assert(std::is_same_v<extract::reduction_accumulator_t<&red_test::f_throws>, int>);
}

void test_inferred_tags_match_input_tag() {
    // A reduction harvests exactly one permission tag, the input
    // region's.  The accumulator's R and Op types are not regions and
    // carry no ownership, so they must not enter the permission set: if
    // they did, the dispatcher would emit permission-split machinery for
    // types that have nothing to split.

    namespace proto = ::crucible::safety::proto;

    using TagsExpected = proto::PermSet<input_tag>;
    static_assert(proto::perm_set_equal_v<extract::inferred_permission_tags_t<&red_test::f_sum_into>, TagsExpected>);

    // One tag, not three.  Three would mean R and Op leaked in.
    static_assert(extract::inferred_permission_tags_count_v<&red_test::f_sum_into> == 1);

    using TagsExpectedOther = proto::PermSet<other_tag>;
    static_assert(
        proto::perm_set_equal_v<extract::inferred_permission_tags_t<&red_test::f_other_tag>, TagsExpectedOther>);

    // A struct accumulator does not change the count either.
    static_assert(extract::inferred_permission_tags_count_v<&red_test::f_hist_into> == 1);

    static_assert(
        extract::function_has_tag_v<&red_test::f_sum_into, extract::reduction_input_tag_t<&red_test::f_sum_into>>);
}

void test_cross_shape_exclusion_with_tag_free() {
    // The dispatcher routes tag-free and tag-bearing functions down
    // disjoint lowering trees, so the two classes must not overlap.  A
    // reduction always carries the input region's tag, and a tag-free
    // function therefore can never satisfy the concept.
    static_assert(!extract::is_tag_free_function_v<&red_test::f_sum_into>);
    static_assert(!extract::is_tag_free_function_v<&red_test::f_max_into>);
    static_assert(!extract::is_tag_free_function_v<&red_test::f_hist_into>);

    static_assert(extract::is_tag_free_function_v<&red_test::f_no_param>);
    static_assert(!extract::Reduction<&red_test::f_no_param>);

    // f_int_first is tag-free even though it has parameters: an int
    // carries no tag, and reduce_into is not a permission wrapper.
    static_assert(extract::is_tag_free_function_v<&red_test::f_int_first>);
    static_assert(!extract::Reduction<&red_test::f_int_first>);
}

void test_runtime_consistency() {
    // The bound is volatile so the loop survives optimization and the
    // recognition result is read more than once.
    volatile std::size_t const cap = 50;
    bool baseline_pos = extract::is_reduction_v<&red_test::f_sum_into>;
    bool baseline_neg = !extract::is_reduction_v<&red_test::f_one_param>;
    EXPECT_TRUE(baseline_pos);
    EXPECT_TRUE(baseline_neg);
    for (std::size_t i = 0; i < cap; ++i) {
        EXPECT_TRUE(baseline_pos == extract::is_reduction_v<&red_test::f_sum_into>);
        EXPECT_TRUE(baseline_neg == !extract::is_reduction_v<&red_test::f_one_param>);
        EXPECT_TRUE(extract::Reduction<&red_test::f_max_into>);
        EXPECT_TRUE(!extract::Reduction<&red_test::f_no_param>);
    }
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_reduction:\n");
    run_test("test_runtime_smoke", test_runtime_smoke);
    run_test("test_positive_sum_into", test_positive_sum_into);
    run_test("test_positive_max_into", test_positive_max_into);
    run_test("test_positive_hist_into", test_positive_hist_into);
    run_test("test_positive_distinct_tag", test_positive_distinct_tag);
    run_test("test_positive_distinct_input_element", test_positive_distinct_input_element);
    run_test("test_volatile_qualified_admitted", test_volatile_qualified_admitted);
    run_test("test_negative_arity_mismatch", test_negative_arity_mismatch);
    run_test("test_negative_accumulator_rvalue_ref", test_negative_accumulator_rvalue_ref);
    run_test("test_negative_accumulator_const_ref", test_negative_accumulator_const_ref);
    run_test("test_negative_input_lvalue_ref", test_negative_input_lvalue_ref);
    run_test("test_negative_input_const_rvalue", test_negative_input_const_rvalue);
    run_test("test_negative_swapped_order", test_negative_swapped_order);
    run_test("test_negative_int_return", test_negative_int_return);
    run_test("test_negative_int_first", test_negative_int_first);
    run_test("test_negative_non_reduce_into_second", test_negative_non_reduce_into_second);
    run_test("test_input_tag_extraction", test_input_tag_extraction);
    run_test("test_input_value_extraction", test_input_value_extraction);
    run_test("test_accumulator_extraction", test_accumulator_extraction);
    run_test("test_reducer_extraction", test_reducer_extraction);
    run_test("test_concept_form_in_constraints", test_concept_form_in_constraints);
    run_test("test_cross_shape_exclusion_with_unary", test_cross_shape_exclusion_with_unary);
    run_test("test_cross_shape_exclusion_with_binary", test_cross_shape_exclusion_with_binary);
    run_test("test_cross_shape_exclusion_with_producer_endpoint", test_cross_shape_exclusion_with_producer_endpoint);
    run_test("test_cross_shape_exclusion_with_consumer_endpoint", test_cross_shape_exclusion_with_consumer_endpoint);
    run_test("test_cross_shape_exclusion_with_swmr_writer", test_cross_shape_exclusion_with_swmr_writer);
    run_test("test_cross_shape_exclusion_with_swmr_reader", test_cross_shape_exclusion_with_swmr_reader);
    run_test("test_cross_shape_exclusion_with_pipeline_stage", test_cross_shape_exclusion_with_pipeline_stage);
    run_test("test_noexcept_false_admitted", test_noexcept_false_admitted);
    run_test("test_inferred_tags_match_input_tag", test_inferred_tags_match_input_tag);
    run_test("test_cross_shape_exclusion_with_tag_free", test_cross_shape_exclusion_with_tag_free);
    run_test("test_runtime_consistency", test_runtime_consistency);
    std::fprintf(stderr, "\n%d passed, %d failed\n", total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
