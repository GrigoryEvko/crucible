// ═══════════════════════════════════════════════════════════════════
// test_reduction — sentinel TU for safety/Reduction.h (FOUND-D14)
//
// Same blind-spot rationale as test_unary_transform / test_binary_transform:
// forces Reduction.h through the full -Werror=* matrix and exercises
// the runtime smoke test.
//
// Coverage (broader than header self-test):
//   * Positive: sum_into — int + plus reducer.
//   * Positive: max_into — int + max reducer.
//   * Positive: hist_into — std::array<int, N> + hist_op reducer.
//   * Positive: distinct R types yield distinct trait specializations.
//   * Positive: input tag and accumulator R types extracted correctly.
//   * Positive: volatile&& on input, volatile& on accumulator admitted
//               (volatile is orthogonal to ownership/borrow semantics).
//   * Negative: arity 0, 1, 3.
//   * Negative: accumulator by rvalue ref (would consume — defeats
//               iterative refinement).
//   * Negative: accumulator by const lvalue ref (cannot mutate).
//   * Negative: input by lvalue ref (borrow, not consume).
//   * Negative: input by const rvalue ref (cannot move from const).
//   * Negative: swapped parameter order (RI& first, OR&& second).
//   * Negative: non-void return.
//   * Negative: non-region first parameter.
//   * Negative: non-reduce_into second parameter.
//   * Cross-shape exclusion: Reduction ⊥ UnaryTransform.
//   * Cross-shape exclusion: Reduction ⊥ BinaryTransform.
//   * Extraction: input_tag / input_value / accumulator / reducer
//                 work across all worked examples.
// ═══════════════════════════════════════════════════════════════════

#include <crucible/safety/Reduction.h>

#include <crucible/safety/BinaryTransform.h>
#include <crucible/safety/OwnedRegion.h>
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

struct input_tag {};
struct other_tag {};

struct PlusOp {
    constexpr int operator()(int const& a, int const& b) const noexcept {
        return a + b;
    }
};

struct MaxOp {
    constexpr int operator()(int const& a, int const& b) const noexcept {
        return a > b ? a : b;
    }
};

using HistArray = std::array<int, 256>;

struct HistOp {
    constexpr HistArray operator()(
        HistArray const& a, HistArray const& b) const noexcept {
        HistArray out{};
        for (std::size_t i = 0; i < a.size(); ++i) {
            out[i] = a[i] + b[i];
        }
        return out;
    }
};

}  // namespace

namespace red_test {

using OR_int_input    = ::crucible::safety::OwnedRegion<int,    ::input_tag>;
using OR_float_input  = ::crucible::safety::OwnedRegion<float,  ::input_tag>;
using OR_int_other    = ::crucible::safety::OwnedRegion<int,    ::other_tag>;

using RI_int_plus     = ::crucible::safety::reduce_into<int,       ::PlusOp>;
using RI_int_max      = ::crucible::safety::reduce_into<int,       ::MaxOp>;
using RI_hist         = ::crucible::safety::reduce_into<::HistArray, ::HistOp>;

// ── Positive shapes ─────────────────────────────────────────────

// Canonical sum_into shape — int input, int+plus accumulator.
void f_sum_into(OR_int_input&&, RI_int_plus&) noexcept;

// max_into — int input, int+max accumulator (non-commutative-tier
// concerns are out-of-scope for the concept; concept admits any
// associative reducer).
void f_max_into(OR_int_input&&, RI_int_max&) noexcept;

// hist_into — int input, struct accumulator + struct reducer.
// Same shape, different R/Op type pair.
void f_hist_into(OR_int_input&&, RI_hist&) noexcept;

// Distinct input tag — Reduction admits any input Tag.
void f_other_tag(OR_int_other&&, RI_int_plus&) noexcept;

// Different input element type.
void f_float_input(OR_float_input&&, RI_int_plus&) noexcept;

// ── Volatile-qualified shapes (admitted — orthogonal axis) ──────

// Input volatile&&: still rvalue-ref to a non-const OwnedRegion.
void f_volatile_input(OR_int_input volatile&&, RI_int_plus&) noexcept;

// Accumulator volatile&: still lvalue-ref to a non-const reduce_into.
void f_volatile_accumulator(OR_int_input&&, RI_int_plus volatile&) noexcept;

// ── Negative shapes ─────────────────────────────────────────────

void f_no_param() noexcept;
void f_one_param(OR_int_input&&) noexcept;
void f_three_params(OR_int_input&&, RI_int_plus&, int) noexcept;

// Accumulator by rvalue ref — would CONSUME the accumulator,
// defeating iterative refinement (caller wants the partial state
// to outlive the call).
void f_accumulator_rvalue(OR_int_input&&, RI_int_plus&&) noexcept;

// Accumulator by const lvalue ref — cannot mutate; reducer cannot
// make progress.
void f_accumulator_const_ref(OR_int_input&&, RI_int_plus const&) noexcept;

// Input by lvalue ref — BORROW shape, not consume; falls through
// to a different per-shape lowering.
void f_input_lvalue(OR_int_input&, RI_int_plus&) noexcept;

// Input by const rvalue ref — cannot move from const.
void f_input_const_rvalue(OR_int_input const&&, RI_int_plus&) noexcept;

// Swapped parameter order — accumulator first, input second.  The
// dispatcher's permission generation hard-codes "param 0 is the
// consumed input"; swapping is a structural mismatch.
void f_swapped_order(RI_int_plus&, OR_int_input&&) noexcept;

// Non-void return — reducer result already lives in the borrowed
// accumulator; a separate return is redundant and ambiguous.
int  f_int_return(OR_int_input&&, RI_int_plus&) noexcept;

// Non-region first parameter.
void f_int_first(int, RI_int_plus&) noexcept;

// Non-reduce_into second parameter.
void f_non_reduce_into(OR_int_input&&, int&) noexcept;

}  // namespace red_test

namespace {

void test_runtime_smoke() {
    EXPECT_TRUE(extract::reduction_smoke_test());
}

// ── Positive-shape recognition ──────────────────────────────────

void test_positive_sum_into() {
    static_assert( extract::Reduction<&red_test::f_sum_into>);
    static_assert( extract::is_reduction_v<&red_test::f_sum_into>);
}

void test_positive_max_into() {
    static_assert( extract::Reduction<&red_test::f_max_into>);
}

void test_positive_hist_into() {
    static_assert( extract::Reduction<&red_test::f_hist_into>);
}

void test_positive_distinct_tag() {
    static_assert( extract::Reduction<&red_test::f_other_tag>);
}

void test_positive_distinct_input_element() {
    static_assert( extract::Reduction<&red_test::f_float_input>);
}

void test_volatile_qualified_admitted() {
    // Volatile is orthogonal to the consume-vs-borrow semantics the
    // concept structurally encodes — admit on either parameter.
    static_assert( extract::Reduction<&red_test::f_volatile_input>);
    static_assert( extract::Reduction<&red_test::f_volatile_accumulator>);
}

// ── Negative-shape rejection ────────────────────────────────────

void test_negative_arity_mismatch() {
    static_assert(!extract::Reduction<&red_test::f_no_param>);
    static_assert(!extract::Reduction<&red_test::f_one_param>);
    static_assert(!extract::Reduction<&red_test::f_three_params>);
}

void test_negative_accumulator_rvalue_ref() {
    static_assert(!extract::Reduction<&red_test::f_accumulator_rvalue>);
}

void test_negative_accumulator_const_ref() {
    static_assert(!extract::Reduction<&red_test::f_accumulator_const_ref>);
}

void test_negative_input_lvalue_ref() {
    static_assert(!extract::Reduction<&red_test::f_input_lvalue>);
}

void test_negative_input_const_rvalue() {
    static_assert(!extract::Reduction<&red_test::f_input_const_rvalue>);
}

void test_negative_swapped_order() {
    static_assert(!extract::Reduction<&red_test::f_swapped_order>);
}

void test_negative_int_return() {
    static_assert(!extract::Reduction<&red_test::f_int_return>);
}

void test_negative_int_first() {
    static_assert(!extract::Reduction<&red_test::f_int_first>);
}

void test_negative_non_reduce_into_second() {
    static_assert(!extract::Reduction<&red_test::f_non_reduce_into>);
}

// ── Extractors ──────────────────────────────────────────────────

void test_input_tag_extraction() {
    static_assert(std::is_same_v<
        extract::reduction_input_tag_t<&red_test::f_sum_into>,
        input_tag>);
    static_assert(std::is_same_v<
        extract::reduction_input_tag_t<&red_test::f_other_tag>,
        other_tag>);
}

void test_input_value_extraction() {
    static_assert(std::is_same_v<
        extract::reduction_input_value_t<&red_test::f_sum_into>,
        int>);
    static_assert(std::is_same_v<
        extract::reduction_input_value_t<&red_test::f_float_input>,
        float>);
}

void test_accumulator_extraction() {
    static_assert(std::is_same_v<
        extract::reduction_accumulator_t<&red_test::f_sum_into>,
        int>);
    static_assert(std::is_same_v<
        extract::reduction_accumulator_t<&red_test::f_hist_into>,
        HistArray>);
    // Distinct accumulator types yield distinct trait specializations.
    EXPECT_TRUE((!std::is_same_v<
        extract::reduction_accumulator_t<&red_test::f_sum_into>,
        extract::reduction_accumulator_t<&red_test::f_hist_into>>));
}

void test_reducer_extraction() {
    static_assert(std::is_same_v<
        extract::reduction_reducer_t<&red_test::f_sum_into>,
        PlusOp>);
    static_assert(std::is_same_v<
        extract::reduction_reducer_t<&red_test::f_max_into>,
        MaxOp>);
    static_assert(std::is_same_v<
        extract::reduction_reducer_t<&red_test::f_hist_into>,
        HistOp>);
    // Same R type, different Op → distinct reducer extractions.
    EXPECT_TRUE((!std::is_same_v<
        extract::reduction_reducer_t<&red_test::f_sum_into>,
        extract::reduction_reducer_t<&red_test::f_max_into>>));
}

// ── Concept form in requires-clauses ────────────────────────────

void test_concept_form_in_constraints() {
    auto callable_with_reduction = []<auto FnPtr>()
        requires extract::Reduction<FnPtr>
    {
        return true;
    };

    EXPECT_TRUE(callable_with_reduction.template operator()<
        &red_test::f_sum_into>());
    EXPECT_TRUE(callable_with_reduction.template operator()<
        &red_test::f_hist_into>());
}

// ── Cross-shape exclusion ────────────────────────────────────────

void test_cross_shape_exclusion_with_unary() {
    // Reduction is arity 2; UnaryTransform is arity 1 — structural
    // exclusion.  A function has AT MOST one canonical shape.
    static_assert( extract::Reduction<&red_test::f_sum_into>);
    static_assert(!extract::UnaryTransform<&red_test::f_sum_into>);

    static_assert(!extract::Reduction<&red_test::f_one_param>);
    static_assert( extract::UnaryTransform<&red_test::f_one_param>);
}

void test_cross_shape_exclusion_with_binary() {
    // Reduction's param 1 is reduce_into<R, Op>& (lvalue ref to a
    // borrowed accumulator); BinaryTransform's param 1 is
    // OwnedRegion<U, Tag>&& (rvalue ref to a consumed second
    // input).  The two shapes differ structurally on param 1's
    // wrapper type AND reference category — a function cannot match
    // both simultaneously.
    static_assert( extract::Reduction<&red_test::f_sum_into>);
    static_assert(!extract::BinaryTransform<&red_test::f_sum_into>);
}

// ── Runtime consistency ─────────────────────────────────────────

void test_runtime_consistency() {
    // Volatile-bounded loop confirms recognition is bit-stable.
    volatile std::size_t const cap = 50;
    bool baseline_pos = extract::is_reduction_v<&red_test::f_sum_into>;
    bool baseline_neg = !extract::is_reduction_v<&red_test::f_one_param>;
    EXPECT_TRUE(baseline_pos);
    EXPECT_TRUE(baseline_neg);
    for (std::size_t i = 0; i < cap; ++i) {
        EXPECT_TRUE(baseline_pos
            == extract::is_reduction_v<&red_test::f_sum_into>);
        EXPECT_TRUE(baseline_neg
            == !extract::is_reduction_v<&red_test::f_one_param>);
        EXPECT_TRUE(extract::Reduction<&red_test::f_max_into>);
        EXPECT_TRUE(!extract::Reduction<&red_test::f_no_param>);
    }
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_reduction:\n");
    run_test("test_runtime_smoke",                    test_runtime_smoke);
    run_test("test_positive_sum_into",                test_positive_sum_into);
    run_test("test_positive_max_into",                test_positive_max_into);
    run_test("test_positive_hist_into",               test_positive_hist_into);
    run_test("test_positive_distinct_tag",            test_positive_distinct_tag);
    run_test("test_positive_distinct_input_element",  test_positive_distinct_input_element);
    run_test("test_volatile_qualified_admitted",      test_volatile_qualified_admitted);
    run_test("test_negative_arity_mismatch",          test_negative_arity_mismatch);
    run_test("test_negative_accumulator_rvalue_ref",  test_negative_accumulator_rvalue_ref);
    run_test("test_negative_accumulator_const_ref",   test_negative_accumulator_const_ref);
    run_test("test_negative_input_lvalue_ref",        test_negative_input_lvalue_ref);
    run_test("test_negative_input_const_rvalue",      test_negative_input_const_rvalue);
    run_test("test_negative_swapped_order",           test_negative_swapped_order);
    run_test("test_negative_int_return",              test_negative_int_return);
    run_test("test_negative_int_first",               test_negative_int_first);
    run_test("test_negative_non_reduce_into_second",  test_negative_non_reduce_into_second);
    run_test("test_input_tag_extraction",             test_input_tag_extraction);
    run_test("test_input_value_extraction",           test_input_value_extraction);
    run_test("test_accumulator_extraction",           test_accumulator_extraction);
    run_test("test_reducer_extraction",               test_reducer_extraction);
    run_test("test_concept_form_in_constraints",      test_concept_form_in_constraints);
    run_test("test_cross_shape_exclusion_with_unary",  test_cross_shape_exclusion_with_unary);
    run_test("test_cross_shape_exclusion_with_binary", test_cross_shape_exclusion_with_binary);
    run_test("test_runtime_consistency",              test_runtime_consistency);
    std::fprintf(stderr, "\n%d passed, %d failed\n",
                 total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
