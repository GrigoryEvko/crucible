// Including the headers is itself part of the claim.  The static_asserts
// a header-only file carries are never compiled under the project warning
// flags until some translation unit pulls the header in.

#include <crucible/safety/IsReduceInto.h>
#include <crucible/safety/reduce_into.h>

#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <type_traits>
#include <utility>

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

struct PlusOpInt {
    constexpr int operator()(int const& a, int const& b) const noexcept { return a + b; }
};

struct MaxOpInt {
    constexpr int operator()(int const& a, int const& b) const noexcept { return a > b ? a : b; }
};

struct PlusOpDouble {
    constexpr double operator()(double const& a, double const& b) const noexcept { return a + b; }
};

using RI_int_plus = ::crucible::safety::reduce_into<int, PlusOpInt>;
using RI_int_max = ::crucible::safety::reduce_into<int, MaxOpInt>;
using RI_double_plus = ::crucible::safety::reduce_into<double, PlusOpDouble>;

void test_runtime_smoke_reduce_into() { EXPECT_TRUE(::crucible::safety::reduce_into_smoke_test()); }

void test_runtime_smoke_is_reduce_into() { EXPECT_TRUE(extract::is_reduce_into_smoke_test()); }

void test_positive_cases() {
    static_assert(extract::is_reduce_into_v<RI_int_plus>);
    static_assert(extract::is_reduce_into_v<RI_int_max>);
    static_assert(extract::is_reduce_into_v<RI_double_plus>);
}

void test_cvref_stripping() {
    static_assert(extract::is_reduce_into_v<RI_int_plus&>);
    static_assert(extract::is_reduce_into_v<RI_int_plus&&>);
    static_assert(extract::is_reduce_into_v<RI_int_plus const>);
    static_assert(extract::is_reduce_into_v<RI_int_plus const&>);
    static_assert(extract::is_reduce_into_v<RI_int_plus const&&>);
    static_assert(extract::is_reduce_into_v<RI_int_plus volatile>);
    static_assert(extract::is_reduce_into_v<RI_int_plus const volatile>);
}

void test_negative_cases() {
    static_assert(!extract::is_reduce_into_v<int>);
    static_assert(!extract::is_reduce_into_v<int*>);
    static_assert(!extract::is_reduce_into_v<int&>);
    static_assert(!extract::is_reduce_into_v<int&&>);
    static_assert(!extract::is_reduce_into_v<void>);
    static_assert(!extract::is_reduce_into_v<PlusOpInt>);
    static_assert(!extract::is_reduce_into_v<MaxOpInt>);
}

void test_lookalike_rejected() {
    // The trait matches the template specialization, not the field shape.
    struct Lookalike {
        int acc;
        PlusOpInt op;
    };
    static_assert(!extract::is_reduce_into_v<Lookalike>);
}

void test_concept_form() {
    static_assert(extract::IsReduceInto<RI_int_plus>);
    static_assert(extract::IsReduceInto<RI_int_plus&&>);
    static_assert(extract::IsReduceInto<RI_int_plus const&>);
    static_assert(!extract::IsReduceInto<int>);
    static_assert(!extract::IsReduceInto<PlusOpInt>);
}

void test_accumulator_extraction() {
    static_assert(std::is_same_v<extract::reduce_into_accumulator_t<RI_int_plus>, int>);
    static_assert(std::is_same_v<extract::reduce_into_accumulator_t<RI_int_max>, int>);
    static_assert(std::is_same_v<extract::reduce_into_accumulator_t<RI_double_plus>, double>);
}

void test_reducer_extraction() {
    static_assert(std::is_same_v<extract::reduce_into_reducer_t<RI_int_plus>, PlusOpInt>);
    static_assert(std::is_same_v<extract::reduce_into_reducer_t<RI_int_max>, MaxOpInt>);
    static_assert(std::is_same_v<extract::reduce_into_reducer_t<RI_double_plus>, PlusOpDouble>);
}

void test_extraction_cvref_stripped() {
    static_assert(std::is_same_v<extract::reduce_into_accumulator_t<RI_int_plus&>, int>);
    static_assert(std::is_same_v<extract::reduce_into_accumulator_t<RI_int_plus const&>, int>);
    static_assert(std::is_same_v<extract::reduce_into_accumulator_t<RI_int_plus&&>, int>);

    static_assert(std::is_same_v<extract::reduce_into_reducer_t<RI_int_plus&>, PlusOpInt>);
    static_assert(std::is_same_v<extract::reduce_into_reducer_t<RI_int_plus const&>, PlusOpInt>);
}

void test_distinct_specializations() {
    static_assert(std::is_same_v<extract::reduce_into_accumulator_t<RI_int_plus>,
                                 extract::reduce_into_accumulator_t<RI_int_max>>);
    static_assert(
        !std::is_same_v<extract::reduce_into_reducer_t<RI_int_plus>, extract::reduce_into_reducer_t<RI_int_max>>);

    static_assert(!std::is_same_v<extract::reduce_into_accumulator_t<RI_int_plus>,
                                  extract::reduce_into_accumulator_t<RI_double_plus>>);
    static_assert(
        !std::is_same_v<extract::reduce_into_reducer_t<RI_int_plus>, extract::reduce_into_reducer_t<RI_double_plus>>);
}

void test_pointer_to_reduce_into_rejected() {
    // Matching strips cv and reference qualifiers.  It does not strip
    // pointers, so a pointer to a reduce_into is not one itself.
    using PtrRI = RI_int_plus*;
    static_assert(!extract::is_reduce_into_v<PtrRI>);
    static_assert(!extract::is_reduce_into_v<RI_int_plus* const>);
    static_assert(!extract::is_reduce_into_v<RI_int_plus const*>);
    static_assert(!extract::is_reduce_into_v<RI_int_plus* const&>);
}

void test_is_reduction_op_v_positive() {
    using ::crucible::safety::is_reduction_op_v;
    static_assert(is_reduction_op_v<PlusOpInt, int>);
    static_assert(is_reduction_op_v<MaxOpInt, int>);
    static_assert(is_reduction_op_v<PlusOpDouble, double>);

    struct PlusViaStd {
        constexpr int operator()(int const& a, int const& b) const noexcept { return a + b; }
    };
    static_assert(is_reduction_op_v<PlusViaStd, int>);
}

void test_is_reduction_op_v_negative() {
    using ::crucible::safety::is_reduction_op_v;

    struct NotInvocable {};
    static_assert(!is_reduction_op_v<NotInvocable, int>);

    struct WrongArity {
        constexpr int operator()(int) const noexcept { return 0; }
    };
    static_assert(!is_reduction_op_v<WrongArity, int>);

    struct WrongReturn {
        constexpr void operator()(int const&, int const&) const noexcept {}
    };
    static_assert(!is_reduction_op_v<WrongReturn, int>);

    // The predicate asks only for convertibility to R, so a narrowing
    // arithmetic return still satisfies it.  A return type with no
    // conversion to R at all is what fails, and is what is tested here.
    struct ReturnUnconvertible {
        struct NotConvertible {};
        constexpr NotConvertible operator()(int const&, int const&) const noexcept { return {}; }
    };
    static_assert(!is_reduction_op_v<ReturnUnconvertible, int>);
}

void test_reduce_into_construct_peek() {
    RI_int_plus r{0, PlusOpInt{}};
    EXPECT_TRUE(r.peek() == 0);
}

void test_reduce_into_combine() {
    RI_int_plus r{0, PlusOpInt{}};
    r.combine(7);
    r.combine(35);
    EXPECT_TRUE(r.peek() == 42);
}

void test_reduce_into_max_combine() {
    RI_int_max r{0, MaxOpInt{}};
    r.combine(7);
    r.combine(99);
    r.combine(3);
    r.combine(42);
    EXPECT_TRUE(r.peek() == 99);
}

void test_reduce_into_peek_mut() {
    RI_int_plus r{10, PlusOpInt{}};
    r.peek_mut() = 99;
    EXPECT_TRUE(r.peek() == 99);
}

void test_reduce_into_consume() {
    RI_int_plus r{42, PlusOpInt{}};
    int extracted = std::move(r).consume();
    EXPECT_TRUE(extracted == 42);
}

void test_reduce_into_reducer_access() {
    RI_int_plus r{0, PlusOpInt{}};
    auto const& op = r.reducer();
    EXPECT_TRUE(op(3, 4) == 7);
}

void test_reduce_into_move_only() {
    static_assert(!std::is_copy_constructible_v<RI_int_plus>);
    static_assert(!std::is_copy_assignable_v<RI_int_plus>);
    static_assert(std::is_move_constructible_v<RI_int_plus>);
    static_assert(std::is_move_assignable_v<RI_int_plus>);
}

void test_reduce_into_move_preserves_state() {
    RI_int_plus a{0, PlusOpInt{}};
    a.combine(5);
    a.combine(7);
    EXPECT_TRUE(a.peek() == 12);

    RI_int_plus b = std::move(a);
    EXPECT_TRUE(b.peek() == 12);
}

void test_double_reduce_into() {
    // The three addends are negative powers of two, so every partial sum
    // is exact in binary64 and the result is bit-exact 0.875.  The
    // comparison goes through the bit pattern because float equality is
    // a hard error under the project warning flags.
    RI_double_plus r{0.0, PlusOpDouble{}};
    r.combine(0.5);
    r.combine(0.25);
    r.combine(0.125);
    auto const got_bits = std::bit_cast<std::uint64_t>(r.peek());
    auto const want_bits = std::bit_cast<std::uint64_t>(0.875);
    EXPECT_TRUE(got_bits == want_bits);
}

void test_runtime_consistency() {
    // The bound is volatile so the compiler cannot fold the repeated
    // evaluations down to the single constant it can see.
    volatile std::size_t const cap = 50;
    bool baseline = extract::is_reduce_into_v<RI_int_plus>;
    EXPECT_TRUE(baseline);
    for (std::size_t i = 0; i < cap; ++i) {
        EXPECT_TRUE(baseline == extract::is_reduce_into_v<RI_int_plus>);
        EXPECT_TRUE(!extract::is_reduce_into_v<int>);
    }
}

struct StatsAcc {
    int sum = 0;
    int count = 0;
    constexpr bool operator==(StatsAcc const&) const = default;
};

struct MergeStatsOp {
    constexpr StatsAcc operator()(StatsAcc const& a, StatsAcc const& b) const noexcept {
        return StatsAcc{a.sum + b.sum, a.count + b.count};
    }
};

using RI_stats_merge = crucible::safety::reduce_into<StatsAcc, MergeStatsOp>;

void test_struct_accumulator_specialization() {
    // A struct accumulator matches as readily as an arithmetic one.  The
    // trait carries no hidden arithmetic constraint on R.
    EXPECT_TRUE(extract::is_reduce_into_v<RI_stats_merge>);
    EXPECT_TRUE(extract::is_reduce_into_v<RI_stats_merge const&>);
    EXPECT_TRUE(extract::is_reduce_into_v<RI_stats_merge&&>);

    static_assert(std::is_same_v<extract::reduce_into_accumulator_t<RI_stats_merge>, StatsAcc>);
    static_assert(std::is_same_v<extract::reduce_into_reducer_t<RI_stats_merge>, MergeStatsOp>);

    // The extra parentheses stop the comma inside the template argument
    // list from splitting the macro argument.
    EXPECT_TRUE((!std::is_same_v<RI_stats_merge, RI_int_plus>));
    EXPECT_TRUE((!std::is_same_v<extract::reduce_into_accumulator_t<RI_stats_merge>,
                                 extract::reduce_into_accumulator_t<RI_int_plus>>));

    RI_stats_merge r{StatsAcc{}, MergeStatsOp{}};
    r.combine(StatsAcc{3, 1});
    r.combine(StatsAcc{4, 1});
    auto const expected_after_combine = StatsAcc{7, 2};
    EXPECT_TRUE(r.peek() == expected_after_combine);

    StatsAcc const out = std::move(r).consume();
    EXPECT_TRUE(out == expected_after_combine);
}

struct PlusOpReturnsShort {
    constexpr short operator()(long const& a, long const& b) const noexcept { return static_cast<short>(a + b); }
};

struct ReturnsStruct {
    constexpr StatsAcc operator()(int const&, int const&) const noexcept { return StatsAcc{}; }
};

void test_op_return_convertibility() {
    EXPECT_TRUE((crucible::safety::is_reduction_op_v<PlusOpReturnsShort, long>));

    EXPECT_TRUE((!crucible::safety::is_reduction_op_v<ReturnsStruct, int>));

    // This reducer is declared over `long const&` yet satisfies the
    // predicate for narrower R as well.  Both the parameters and the
    // return are matched by convertibility, not by exact type.
    EXPECT_TRUE((crucible::safety::is_reduction_op_v<PlusOpReturnsShort, int>));
    EXPECT_TRUE((crucible::safety::is_reduction_op_v<PlusOpReturnsShort, short>));
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_is_reduce_into:\n");
    run_test("test_runtime_smoke_reduce_into", test_runtime_smoke_reduce_into);
    run_test("test_runtime_smoke_is_reduce_into", test_runtime_smoke_is_reduce_into);
    run_test("test_positive_cases", test_positive_cases);
    run_test("test_cvref_stripping", test_cvref_stripping);
    run_test("test_negative_cases", test_negative_cases);
    run_test("test_lookalike_rejected", test_lookalike_rejected);
    run_test("test_concept_form", test_concept_form);
    run_test("test_accumulator_extraction", test_accumulator_extraction);
    run_test("test_reducer_extraction", test_reducer_extraction);
    run_test("test_extraction_cvref_stripped", test_extraction_cvref_stripped);
    run_test("test_distinct_specializations", test_distinct_specializations);
    run_test("test_pointer_to_reduce_into_rejected", test_pointer_to_reduce_into_rejected);
    run_test("test_is_reduction_op_v_positive", test_is_reduction_op_v_positive);
    run_test("test_is_reduction_op_v_negative", test_is_reduction_op_v_negative);
    run_test("test_reduce_into_construct_peek", test_reduce_into_construct_peek);
    run_test("test_reduce_into_combine", test_reduce_into_combine);
    run_test("test_reduce_into_max_combine", test_reduce_into_max_combine);
    run_test("test_reduce_into_peek_mut", test_reduce_into_peek_mut);
    run_test("test_reduce_into_consume", test_reduce_into_consume);
    run_test("test_reduce_into_reducer_access", test_reduce_into_reducer_access);
    run_test("test_reduce_into_move_only", test_reduce_into_move_only);
    run_test("test_reduce_into_move_preserves_state", test_reduce_into_move_preserves_state);
    run_test("test_double_reduce_into", test_double_reduce_into);
    run_test("test_runtime_consistency", test_runtime_consistency);
    run_test("test_struct_accumulator_specialization", test_struct_accumulator_specialization);
    run_test("test_op_return_convertibility", test_op_return_convertibility);
    std::fprintf(stderr, "\n%d passed, %d failed\n", total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
