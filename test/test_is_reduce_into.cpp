// ═══════════════════════════════════════════════════════════════════
// test_is_reduce_into — sentinel TU for safety/IsReduceInto.h +
//                       safety/reduce_into.h.
//
// Same blind-spot rationale as test_is_owned_region (see
// feedback_header_only_static_assert_blind_spot memory): a header
// shipped with embedded static_asserts is unverified under the
// project warning flags unless a .cpp TU includes it.  This sentinel
// forces both IsReduceInto.h and reduce_into.h through the test
// target's full -Werror=shadow / -Werror=conversion / -Wanalyzer-*
// matrix and exercises the runtime smoke tests.
//
// Coverage:
//   * Foundation header inclusion under full warning flags.
//   * runtime_smoke_test() execution for both reduce_into and
//     is_reduce_into.
//   * Positive: reduce_into<R, Op> for various (R, Op) and every
//     cv-ref qualified form.
//   * Negative: int, int*, int&, void, foreign struct, the bare Op.
//   * IsReduceInto concept form.
//   * Accumulator-type and reducer-type extraction with cv-ref
//     stripping.
//   * Distinct (R, Op) parity / non-parity.
//   * is_reduction_op_v predicate against valid + invalid Op shapes.
//   * reduce_into wrapper construct / combine / consume / peek_mut.
//   * Move-only semantics (copy is rejected at compile time; move
//     is preserved).
// ═══════════════════════════════════════════════════════════════════

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

// Test reducers — three distinct (R, Op) pairs to exercise the
// trait's specialization on both template parameters.
struct PlusOpInt {
    constexpr int operator()(int const& a, int const& b) const noexcept {
        return a + b;
    }
};

struct MaxOpInt {
    constexpr int operator()(int const& a, int const& b) const noexcept {
        return a > b ? a : b;
    }
};

struct PlusOpDouble {
    constexpr double operator()(double const& a, double const& b) const noexcept {
        return a + b;
    }
};

using RI_int_plus    = ::crucible::safety::reduce_into<int,    PlusOpInt>;
using RI_int_max     = ::crucible::safety::reduce_into<int,    MaxOpInt>;
using RI_double_plus = ::crucible::safety::reduce_into<double, PlusOpDouble>;

void test_runtime_smoke_reduce_into() {
    EXPECT_TRUE(::crucible::safety::reduce_into_smoke_test());
}

void test_runtime_smoke_is_reduce_into() {
    EXPECT_TRUE(extract::is_reduce_into_smoke_test());
}

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
    // The bare Op is NOT a reduce_into.
    static_assert(!extract::is_reduce_into_v<PlusOpInt>);
    static_assert(!extract::is_reduce_into_v<MaxOpInt>);
}

void test_lookalike_rejected() {
    // A struct that walks like a reduce_into (acc + op fields) but is
    // not the reduce_into template specialization is rejected.
    struct Lookalike {
        int       acc;
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
    static_assert(std::is_same_v<
        extract::reduce_into_accumulator_t<RI_int_plus>, int>);
    static_assert(std::is_same_v<
        extract::reduce_into_accumulator_t<RI_int_max>, int>);
    static_assert(std::is_same_v<
        extract::reduce_into_accumulator_t<RI_double_plus>, double>);
}

void test_reducer_extraction() {
    static_assert(std::is_same_v<
        extract::reduce_into_reducer_t<RI_int_plus>, PlusOpInt>);
    static_assert(std::is_same_v<
        extract::reduce_into_reducer_t<RI_int_max>, MaxOpInt>);
    static_assert(std::is_same_v<
        extract::reduce_into_reducer_t<RI_double_plus>, PlusOpDouble>);
}

void test_extraction_cvref_stripped() {
    static_assert(std::is_same_v<
        extract::reduce_into_accumulator_t<RI_int_plus&>, int>);
    static_assert(std::is_same_v<
        extract::reduce_into_accumulator_t<RI_int_plus const&>, int>);
    static_assert(std::is_same_v<
        extract::reduce_into_accumulator_t<RI_int_plus&&>, int>);

    static_assert(std::is_same_v<
        extract::reduce_into_reducer_t<RI_int_plus&>, PlusOpInt>);
    static_assert(std::is_same_v<
        extract::reduce_into_reducer_t<RI_int_plus const&>, PlusOpInt>);
}

void test_distinct_specializations() {
    // Same accumulator, different reducer → reducer distinguishes.
    static_assert(std::is_same_v<
        extract::reduce_into_accumulator_t<RI_int_plus>,
        extract::reduce_into_accumulator_t<RI_int_max>>);
    static_assert(!std::is_same_v<
        extract::reduce_into_reducer_t<RI_int_plus>,
        extract::reduce_into_reducer_t<RI_int_max>>);

    // Same reducer-class but bound to different R → both axes
    // distinguish (different reducer types AND different accumulator
    // types because PlusOpInt != PlusOpDouble despite same name).
    static_assert(!std::is_same_v<
        extract::reduce_into_accumulator_t<RI_int_plus>,
        extract::reduce_into_accumulator_t<RI_double_plus>>);
    static_assert(!std::is_same_v<
        extract::reduce_into_reducer_t<RI_int_plus>,
        extract::reduce_into_reducer_t<RI_double_plus>>);
}

void test_pointer_to_reduce_into_rejected() {
    // remove_cvref does NOT strip pointers — a pointer-to-reduce_into
    // is NOT itself a reduce_into.
    using PtrRI = RI_int_plus*;
    static_assert(!extract::is_reduce_into_v<PtrRI>);
    static_assert(!extract::is_reduce_into_v<RI_int_plus* const>);
    static_assert(!extract::is_reduce_into_v<RI_int_plus const*>);
    static_assert(!extract::is_reduce_into_v<RI_int_plus* const&>);
}

void test_is_reduction_op_v_positive() {
    using ::crucible::safety::is_reduction_op_v;
    static_assert(is_reduction_op_v<PlusOpInt,    int>);
    static_assert(is_reduction_op_v<MaxOpInt,     int>);
    static_assert(is_reduction_op_v<PlusOpDouble, double>);

    // Standard library operators satisfy the predicate.  std::plus<>
    // is invocable as Op(R const&, R const&) → R for any arithmetic R.
    struct PlusViaStd {
        constexpr int operator()(int const& a, int const& b) const noexcept {
            return a + b;
        }
    };
    static_assert(is_reduction_op_v<PlusViaStd, int>);
}

void test_is_reduction_op_v_negative() {
    using ::crucible::safety::is_reduction_op_v;

    // Not invocable.
    struct NotInvocable {};
    static_assert(!is_reduction_op_v<NotInvocable, int>);

    // Wrong arity (1 instead of 2).
    struct WrongArity {
        constexpr int operator()(int) const noexcept { return 0; }
    };
    static_assert(!is_reduction_op_v<WrongArity, int>);

    // Wrong return (void instead of R).
    struct WrongReturn {
        constexpr void operator()(int const&, int const&) const noexcept {}
    };
    static_assert(!is_reduction_op_v<WrongReturn, int>);

    // Type mismatch — Op for double, R == int doesn't admit it
    // unless implicit conversion is available.  Predicate checks
    // convertibility, so a double→int return is convertible.
    // A truly mismatching reducer (returns a non-convertible struct)
    // is rejected.
    struct ReturnUnconvertible {
        struct NotConvertible {};
        constexpr NotConvertible operator()(int const&, int const&) const noexcept {
            return {};
        }
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
    static_assert( std::is_move_constructible_v<RI_int_plus>);
    static_assert( std::is_move_assignable_v<RI_int_plus>);
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
    // Float accumulator path — verifies reduce_into works with
    // arithmetic types beyond int.  The accumulator sums values that
    // are all exactly representable in IEEE 754 binary64
    // (0.5, 0.25, 0.125 are negative powers of two), so the result is
    // bit-exact 0.875.  Compare via std::bit_cast to honor the
    // -Werror=float-equal discipline (CLAUDE.md §X).
    RI_double_plus r{0.0, PlusOpDouble{}};
    r.combine(0.5);
    r.combine(0.25);
    r.combine(0.125);
    auto const got_bits =
        std::bit_cast<std::uint64_t>(r.peek());
    auto const want_bits =
        std::bit_cast<std::uint64_t>(0.875);
    EXPECT_TRUE(got_bits == want_bits);
}

void test_runtime_consistency() {
    // Volatile-bounded loop confirms the predicate is bit-stable
    // across a non-trivial number of evaluations.
    volatile std::size_t const cap = 50;
    bool baseline = extract::is_reduce_into_v<RI_int_plus>;
    EXPECT_TRUE(baseline);
    for (std::size_t i = 0; i < cap; ++i) {
        EXPECT_TRUE(baseline == extract::is_reduce_into_v<RI_int_plus>);
        EXPECT_TRUE(!extract::is_reduce_into_v<int>);
    }
}

// ── FOUND-D07-AUDIT — extended positive coverage ───────────────────

// Non-arithmetic R type — confirms the partial specialization
// matches reduce_into<StructR, Op> as readily as reduce_into<int, Op>,
// with no hidden arithmetic-type assumption in the trait machinery.
struct StatsAcc {
    int sum   = 0;
    int count = 0;
    constexpr bool operator==(StatsAcc const&) const = default;
};

struct MergeStatsOp {
    constexpr StatsAcc operator()(
        StatsAcc const& a, StatsAcc const& b) const noexcept {
        return StatsAcc{a.sum + b.sum, a.count + b.count};
    }
};

using RI_stats_merge =
    crucible::safety::reduce_into<StatsAcc, MergeStatsOp>;

void test_struct_accumulator_specialization() {
    // is_reduce_into_v matches with a struct R type just as it does
    // with an arithmetic R type — confirms the specialization
    // machinery is not silently constrained on `std::is_arithmetic_v`.
    EXPECT_TRUE(extract::is_reduce_into_v<RI_stats_merge>);
    EXPECT_TRUE(extract::is_reduce_into_v<RI_stats_merge const&>);
    EXPECT_TRUE(extract::is_reduce_into_v<RI_stats_merge&&>);

    // Extractors round-trip the struct types.
    static_assert(std::is_same_v<
        extract::reduce_into_accumulator_t<RI_stats_merge>, StatsAcc>);
    static_assert(std::is_same_v<
        extract::reduce_into_reducer_t<RI_stats_merge>, MergeStatsOp>);

    // Distinct specialization vs the arithmetic-R case.  Extra parens
    // wrap commas inside is_same_v's template args so EXPECT_TRUE's
    // function-like-macro parser sees a single argument.
    EXPECT_TRUE((!std::is_same_v<RI_stats_merge, RI_int_plus>));
    EXPECT_TRUE((!std::is_same_v<
        extract::reduce_into_accumulator_t<RI_stats_merge>,
        extract::reduce_into_accumulator_t<RI_int_plus>>));

    // Wrapper itself works on the struct R: combine folds, peek
    // returns an equal struct, consume extracts.
    RI_stats_merge r{StatsAcc{}, MergeStatsOp{}};
    r.combine(StatsAcc{3, 1});
    r.combine(StatsAcc{4, 1});
    auto const expected_after_combine = StatsAcc{7, 2};
    EXPECT_TRUE(r.peek() == expected_after_combine);

    StatsAcc const out = std::move(r).consume();
    EXPECT_TRUE(out == expected_after_combine);
}

// Op whose return type is convertible to R but not exactly R —
// confirms the is_reduction_op_v concept's `convertible_to` clause is
// load-bearing.  PlusOpReturnsShort returns short (a narrowing cast
// from int + int reduced through `static_cast`); short is convertible
// to long, so is_reduction_op_v<PlusOpReturnsShort, long> must hold.
struct PlusOpReturnsShort {
    constexpr short operator()(
        long const& a, long const& b) const noexcept {
        return static_cast<short>(a + b);
    }
};

// Op whose return type is NOT convertible to R — the concept must
// reject.  ReturnsStruct returns StatsAcc which is not convertible
// to int.
struct ReturnsStruct {
    constexpr StatsAcc operator()(
        int const&, int const&) const noexcept {
        return StatsAcc{};
    }
};

void test_op_return_convertibility() {
    // Positive — short is convertible to long → satisfies concept.
    EXPECT_TRUE((crucible::safety::is_reduction_op_v<
                    PlusOpReturnsShort, long>));

    // Negative — StatsAcc is not convertible to int → fails concept.
    EXPECT_TRUE((!crucible::safety::is_reduction_op_v<
                    ReturnsStruct, int>));

    // Same Op + different R re-evaluates the convertibility clause:
    // short→int is fine, short→short is fine.
    EXPECT_TRUE((crucible::safety::is_reduction_op_v<
                    PlusOpReturnsShort, int>));
    // PlusOpReturnsShort takes `long const&` — it is callable with
    // `int const&` (implicit conversion); concept admits.
    EXPECT_TRUE((crucible::safety::is_reduction_op_v<
                    PlusOpReturnsShort, short>));
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_is_reduce_into:\n");
    run_test("test_runtime_smoke_reduce_into",     test_runtime_smoke_reduce_into);
    run_test("test_runtime_smoke_is_reduce_into",  test_runtime_smoke_is_reduce_into);
    run_test("test_positive_cases",                test_positive_cases);
    run_test("test_cvref_stripping",               test_cvref_stripping);
    run_test("test_negative_cases",                test_negative_cases);
    run_test("test_lookalike_rejected",            test_lookalike_rejected);
    run_test("test_concept_form",                  test_concept_form);
    run_test("test_accumulator_extraction",        test_accumulator_extraction);
    run_test("test_reducer_extraction",            test_reducer_extraction);
    run_test("test_extraction_cvref_stripped",     test_extraction_cvref_stripped);
    run_test("test_distinct_specializations",      test_distinct_specializations);
    run_test("test_pointer_to_reduce_into_rejected",
                                                   test_pointer_to_reduce_into_rejected);
    run_test("test_is_reduction_op_v_positive",    test_is_reduction_op_v_positive);
    run_test("test_is_reduction_op_v_negative",    test_is_reduction_op_v_negative);
    run_test("test_reduce_into_construct_peek",    test_reduce_into_construct_peek);
    run_test("test_reduce_into_combine",           test_reduce_into_combine);
    run_test("test_reduce_into_max_combine",       test_reduce_into_max_combine);
    run_test("test_reduce_into_peek_mut",          test_reduce_into_peek_mut);
    run_test("test_reduce_into_consume",           test_reduce_into_consume);
    run_test("test_reduce_into_reducer_access",    test_reduce_into_reducer_access);
    run_test("test_reduce_into_move_only",         test_reduce_into_move_only);
    run_test("test_reduce_into_move_preserves_state",
                                                   test_reduce_into_move_preserves_state);
    run_test("test_double_reduce_into",            test_double_reduce_into);
    run_test("test_runtime_consistency",           test_runtime_consistency);
    run_test("test_struct_accumulator_specialization",
                                                   test_struct_accumulator_specialization);
    run_test("test_op_return_convertibility",      test_op_return_convertibility);
    std::fprintf(stderr, "\n%d passed, %d failed\n",
                 total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
