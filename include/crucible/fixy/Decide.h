#pragma once

// These names live in crucible::decide.  The re-export lets a caller that
// pulls in only the fixy surface reach them without naming that namespace.

#include <crucible/safety/Decide.h>
#include <crucible/safety/DecideOracle.h>

namespace crucible::fixy::decide {

using ::crucible::decide::no_overflow_mul;
using ::crucible::decide::no_overflow_sum;
using ::crucible::decide::no_overflow_pow2_shift;

using ::crucible::decide::all_in_range;
using ::crucible::decide::strictly_increasing;
using ::crucible::decide::weakly_increasing;

using ::crucible::decide::is_power_of_two_le;
using ::crucible::decide::factorization_eq;
using ::crucible::decide::coprime;

using ::crucible::decide::Interval;
using ::crucible::decide::intervals_pairwise_disjoint;
using ::crucible::decide::intervals_cover_unit;

using ::crucible::decide::tier_replaces;
using ::crucible::decide::row_subset;

using ::crucible::decide::fmix_preserves_non_zero;

using ::crucible::decide::conjunction;
using ::crucible::decide::disjunction;
using ::crucible::decide::implies;

using ::crucible::decide::aligned_in_range;
using ::crucible::decide::in_range;
using ::crucible::decide::positive;
using ::crucible::decide::non_negative;

using ::crucible::decide::valid_span;
using ::crucible::decide::is_non_zero;

}  // namespace crucible::fixy::decide

namespace crucible::fixy::decide::self_test {

// The witnesses compare function-pointer types rather than function-pointer
// values, because -Werror=tautological-compare fires once the optimizer
// proves both sides name the same symbol.  Type-level identity is equivalent
// here for any using-declaration that introduces no new overload.

inline constexpr bool same_no_overflow_mul_v = std::is_same_v<decltype(&::crucible::fixy::decide::no_overflow_mul<int>),
                                                              decltype(&::crucible::decide::no_overflow_mul<int>)>;
static_assert(same_no_overflow_mul_v, "fixy::decide::no_overflow_mul must alias the substrate symbol.");

inline constexpr bool same_no_overflow_sum_v = std::is_same_v<decltype(&::crucible::fixy::decide::no_overflow_sum<int>),
                                                              decltype(&::crucible::decide::no_overflow_sum<int>)>;
static_assert(same_no_overflow_sum_v, "fixy::decide::no_overflow_sum must alias the substrate symbol.");

inline constexpr bool same_no_overflow_pow2_shift_v =
    std::is_same_v<decltype(&::crucible::fixy::decide::no_overflow_pow2_shift<int>),
                   decltype(&::crucible::decide::no_overflow_pow2_shift<int>)>;
static_assert(same_no_overflow_pow2_shift_v, "fixy::decide::no_overflow_pow2_shift must alias the substrate symbol.");

inline constexpr bool same_all_in_range_v = std::is_same_v<decltype(&::crucible::fixy::decide::all_in_range<int>),
                                                           decltype(&::crucible::decide::all_in_range<int>)>;
static_assert(same_all_in_range_v, "fixy::decide::all_in_range must alias the substrate symbol.");

inline constexpr bool same_strictly_increasing_v =
    std::is_same_v<decltype(&::crucible::fixy::decide::strictly_increasing<int>),
                   decltype(&::crucible::decide::strictly_increasing<int>)>;
static_assert(same_strictly_increasing_v, "fixy::decide::strictly_increasing must alias the substrate symbol.");

inline constexpr bool same_weakly_increasing_v =
    std::is_same_v<decltype(&::crucible::fixy::decide::weakly_increasing<int>),
                   decltype(&::crucible::decide::weakly_increasing<int>)>;
static_assert(same_weakly_increasing_v, "fixy::decide::weakly_increasing must alias the substrate symbol.");

inline constexpr bool same_is_power_of_two_le_v =
    std::is_same_v<decltype(&::crucible::fixy::decide::is_power_of_two_le<int>),
                   decltype(&::crucible::decide::is_power_of_two_le<int>)>;
static_assert(same_is_power_of_two_le_v, "fixy::decide::is_power_of_two_le must alias the substrate symbol.");

inline constexpr bool same_factorization_eq_v =
    std::is_same_v<decltype(&::crucible::fixy::decide::factorization_eq<int>),
                   decltype(&::crucible::decide::factorization_eq<int>)>;
static_assert(same_factorization_eq_v, "fixy::decide::factorization_eq must alias the substrate symbol.");

inline constexpr bool same_coprime_v =
    std::is_same_v<decltype(&::crucible::fixy::decide::coprime<int>), decltype(&::crucible::decide::coprime<int>)>;
static_assert(same_coprime_v, "fixy::decide::coprime must alias the substrate symbol.");

static_assert(std::is_same_v<::crucible::fixy::decide::Interval<int>, ::crucible::decide::Interval<int>>,
              "fixy::decide::Interval must alias the substrate type.");

inline constexpr bool same_intervals_pairwise_disjoint_v =
    std::is_same_v<decltype(&::crucible::fixy::decide::intervals_pairwise_disjoint<int>),
                   decltype(&::crucible::decide::intervals_pairwise_disjoint<int>)>;
static_assert(same_intervals_pairwise_disjoint_v,
              "fixy::decide::intervals_pairwise_disjoint must alias the substrate.");

inline constexpr bool same_intervals_cover_unit_v =
    std::is_same_v<decltype(&::crucible::fixy::decide::intervals_cover_unit<int>),
                   decltype(&::crucible::decide::intervals_cover_unit<int>)>;
static_assert(same_intervals_cover_unit_v, "fixy::decide::intervals_cover_unit must alias the substrate.");

// tier_replaces accepts any tier tag.  This enum stands in for one.
namespace decide_self_test_tags {
enum class T {
    A,
    B
};
}  // namespace decide_self_test_tags

inline constexpr bool same_tier_replaces_v =
    std::is_same_v<decltype(&::crucible::fixy::decide::tier_replaces<decide_self_test_tags::T>),
                   decltype(&::crucible::decide::tier_replaces<decide_self_test_tags::T>)>;
static_assert(same_tier_replaces_v, "fixy::decide::tier_replaces must alias the substrate.");

inline constexpr bool same_row_subset_v = std::is_same_v<decltype(&::crucible::fixy::decide::row_subset<int, int>),
                                                         decltype(&::crucible::decide::row_subset<int, int>)>;
static_assert(same_row_subset_v, "fixy::decide::row_subset must alias the substrate.");

inline constexpr bool same_fmix_preserves_non_zero_v =
    std::is_same_v<decltype(&::crucible::fixy::decide::fmix_preserves_non_zero),
                   decltype(&::crucible::decide::fmix_preserves_non_zero)>;
static_assert(same_fmix_preserves_non_zero_v, "fixy::decide::fmix_preserves_non_zero must alias the substrate.");

inline constexpr bool same_conjunction_v =
    std::is_same_v<decltype(&::crucible::fixy::decide::conjunction), decltype(&::crucible::decide::conjunction)>;
static_assert(same_conjunction_v, "fixy::decide::conjunction must alias the substrate.");

inline constexpr bool same_disjunction_v =
    std::is_same_v<decltype(&::crucible::fixy::decide::disjunction), decltype(&::crucible::decide::disjunction)>;
static_assert(same_disjunction_v, "fixy::decide::disjunction must alias the substrate.");

inline constexpr bool same_implies_v =
    std::is_same_v<decltype(&::crucible::fixy::decide::implies), decltype(&::crucible::decide::implies)>;
static_assert(same_implies_v, "fixy::decide::implies must alias the substrate.");

inline constexpr bool same_aligned_in_range_v = std::is_same_v<decltype(&::crucible::fixy::decide::aligned_in_range),
                                                               decltype(&::crucible::decide::aligned_in_range)>;
static_assert(same_aligned_in_range_v, "fixy::decide::aligned_in_range must alias the substrate.");

inline constexpr bool same_in_range_v =
    std::is_same_v<decltype(&::crucible::fixy::decide::in_range<int>), decltype(&::crucible::decide::in_range<int>)>;
static_assert(same_in_range_v, "fixy::decide::in_range must alias the substrate.");

inline constexpr bool same_positive_v =
    std::is_same_v<decltype(&::crucible::fixy::decide::positive<int>), decltype(&::crucible::decide::positive<int>)>;
static_assert(same_positive_v, "fixy::decide::positive must alias the substrate.");

inline constexpr bool same_non_negative_v = std::is_same_v<decltype(&::crucible::fixy::decide::non_negative<int>),
                                                           decltype(&::crucible::decide::non_negative<int>)>;
static_assert(same_non_negative_v, "fixy::decide::non_negative must alias the substrate.");

inline constexpr bool same_valid_span_v = std::is_same_v<decltype(&::crucible::fixy::decide::valid_span<int>),
                                                         decltype(&::crucible::decide::valid_span<int>)>;
static_assert(same_valid_span_v, "fixy::decide::valid_span must alias the substrate.");

inline constexpr bool same_is_non_zero_v = std::is_same_v<decltype(&::crucible::fixy::decide::is_non_zero<int>),
                                                          decltype(&::crucible::decide::is_non_zero<int>)>;
static_assert(same_is_non_zero_v, "fixy::decide::is_non_zero must alias the substrate.");

inline constexpr int kFixyDecidePredicateCount = 23;
inline constexpr int kFixyDecideTypeCount = 1;

static_assert(kFixyDecidePredicateCount >= 20 && kFixyDecidePredicateCount <= 30,
              "fixy::decide predicate catalog drift outside expected window — "
              "audit the using-decl rows above against catalog additions/removals.");

static_assert(kFixyDecidePredicateCount == 23, "ceiling: fixy::decide:: re-exports exactly 23 predicates.  If "
                                               "you add or remove a predicate, update BOTH the constant AND "
                                               "this colocated ceiling pin in the same edit.");

static_assert(kFixyDecideTypeCount == 1, "ceiling: fixy::decide:: re-exports exactly 1 type (Interval<T>).  "
                                         "If you add a second decide type, update BOTH the constant AND "
                                         "this colocated ceiling pin in the same edit.");

}  // namespace crucible::fixy::decide::self_test

namespace crucible::fixy::decide::self_test {

// Non-constant values keep the consteval fast path from masking an
// inline-body fault.
inline void runtime_smoke_test() {
    volatile int a = 7;
    volatile int b = 3;
    volatile int c = 100;
    volatile bool sink = false;
    sink = ::crucible::fixy::decide::no_overflow_mul(a, b);
    sink = ::crucible::fixy::decide::no_overflow_sum(a, b);
    sink = ::crucible::fixy::decide::in_range(a, 0, c);
    sink = ::crucible::fixy::decide::positive(a);
    sink = ::crucible::fixy::decide::non_negative(a);
    sink = ::crucible::fixy::decide::coprime(a, b);
    sink = ::crucible::fixy::decide::is_power_of_two_le(a, c);
    int xs[] = {1, 2, 3, 4, 5};
    sink = ::crucible::fixy::decide::strictly_increasing(std::span<const int>{xs});
    sink = ::crucible::fixy::decide::weakly_increasing(std::span<const int>{xs});
    sink = ::crucible::fixy::decide::all_in_range(std::span<const int>{xs}, 0, 10);
    sink = ::crucible::fixy::decide::implies(true, true);
    bool bs[] = {true, true, false};
    sink = ::crucible::fixy::decide::conjunction(std::span<const bool>{bs});
    sink = ::crucible::fixy::decide::disjunction(std::span<const bool>{bs});
    sink = ::crucible::fixy::decide::is_non_zero(a);
    (void)sink;
}

}  // namespace crucible::fixy::decide::self_test

// The oracles are deliberately slow, transparently correct reference
// implementations.  A property harness compares the production predicates
// against them.
namespace crucible::fixy::decide::oracle {

using ::crucible::decide::oracle::widen;
using ::crucible::decide::oracle::widen_t;
using ::crucible::decide::oracle::no_overflow_mul_oracle;
using ::crucible::decide::oracle::no_overflow_sum_oracle;
using ::crucible::decide::oracle::all_in_range_oracle;
using ::crucible::decide::oracle::strictly_increasing_oracle;
using ::crucible::decide::oracle::weakly_increasing_oracle;
using ::crucible::decide::oracle::is_power_of_two_le_oracle;
using ::crucible::decide::oracle::factorization_eq_oracle;
using ::crucible::decide::oracle::coprime_oracle;
using ::crucible::decide::oracle::conjunction_oracle;
using ::crucible::decide::oracle::disjunction_oracle;
using ::crucible::decide::oracle::aligned_in_range_oracle;

}  // namespace crucible::fixy::decide::oracle

namespace crucible::fixy::decide::oracle::self_test {

static_assert(std::is_same_v<::crucible::fixy::decide::oracle::widen_t<int>, ::crucible::decide::oracle::widen_t<int>>,
              "fixy::decide::oracle::widen_t must alias the substrate.");

inline constexpr bool same_no_overflow_mul_oracle_v =
    std::is_same_v<decltype(&::crucible::fixy::decide::oracle::no_overflow_mul_oracle<int>),
                   decltype(&::crucible::decide::oracle::no_overflow_mul_oracle<int>)>;
static_assert(same_no_overflow_mul_oracle_v, "fixy::decide::oracle::no_overflow_mul_oracle must alias the substrate.");

inline constexpr bool same_no_overflow_sum_oracle_v =
    std::is_same_v<decltype(&::crucible::fixy::decide::oracle::no_overflow_sum_oracle<int>),
                   decltype(&::crucible::decide::oracle::no_overflow_sum_oracle<int>)>;
static_assert(same_no_overflow_sum_oracle_v, "fixy::decide::oracle::no_overflow_sum_oracle must alias the substrate.");

inline constexpr bool same_all_in_range_oracle_v =
    std::is_same_v<decltype(&::crucible::fixy::decide::oracle::all_in_range_oracle<int>),
                   decltype(&::crucible::decide::oracle::all_in_range_oracle<int>)>;
static_assert(same_all_in_range_oracle_v, "fixy::decide::oracle::all_in_range_oracle must alias the substrate.");

inline constexpr bool same_strictly_increasing_oracle_v =
    std::is_same_v<decltype(&::crucible::fixy::decide::oracle::strictly_increasing_oracle<int>),
                   decltype(&::crucible::decide::oracle::strictly_increasing_oracle<int>)>;
static_assert(same_strictly_increasing_oracle_v,
              "fixy::decide::oracle::strictly_increasing_oracle must alias the substrate.");

inline constexpr bool same_weakly_increasing_oracle_v =
    std::is_same_v<decltype(&::crucible::fixy::decide::oracle::weakly_increasing_oracle<int>),
                   decltype(&::crucible::decide::oracle::weakly_increasing_oracle<int>)>;
static_assert(same_weakly_increasing_oracle_v,
              "fixy::decide::oracle::weakly_increasing_oracle must alias the substrate.");

inline constexpr bool same_is_power_of_two_le_oracle_v =
    std::is_same_v<decltype(&::crucible::fixy::decide::oracle::is_power_of_two_le_oracle<int>),
                   decltype(&::crucible::decide::oracle::is_power_of_two_le_oracle<int>)>;
static_assert(same_is_power_of_two_le_oracle_v,
              "fixy::decide::oracle::is_power_of_two_le_oracle must alias the substrate.");

inline constexpr bool same_factorization_eq_oracle_v =
    std::is_same_v<decltype(&::crucible::fixy::decide::oracle::factorization_eq_oracle<int>),
                   decltype(&::crucible::decide::oracle::factorization_eq_oracle<int>)>;
static_assert(same_factorization_eq_oracle_v,
              "fixy::decide::oracle::factorization_eq_oracle must alias the substrate.");

inline constexpr bool same_coprime_oracle_v =
    std::is_same_v<decltype(&::crucible::fixy::decide::oracle::coprime_oracle<int>),
                   decltype(&::crucible::decide::oracle::coprime_oracle<int>)>;
static_assert(same_coprime_oracle_v, "fixy::decide::oracle::coprime_oracle must alias the substrate.");

inline constexpr bool same_conjunction_oracle_v =
    std::is_same_v<decltype(&::crucible::fixy::decide::oracle::conjunction_oracle),
                   decltype(&::crucible::decide::oracle::conjunction_oracle)>;
static_assert(same_conjunction_oracle_v, "fixy::decide::oracle::conjunction_oracle must alias the substrate.");

inline constexpr bool same_disjunction_oracle_v =
    std::is_same_v<decltype(&::crucible::fixy::decide::oracle::disjunction_oracle),
                   decltype(&::crucible::decide::oracle::disjunction_oracle)>;
static_assert(same_disjunction_oracle_v, "fixy::decide::oracle::disjunction_oracle must alias the substrate.");

inline constexpr bool same_aligned_in_range_oracle_v =
    std::is_same_v<decltype(&::crucible::fixy::decide::oracle::aligned_in_range_oracle),
                   decltype(&::crucible::decide::oracle::aligned_in_range_oracle)>;
static_assert(same_aligned_in_range_oracle_v,
              "fixy::decide::oracle::aligned_in_range_oracle must alias the substrate.");

inline constexpr int kFixyDecideOracleCount = 11;
static_assert(kFixyDecideOracleCount >= 11, "fixy::decide::oracle floor — an oracle re-export went missing.");
static_assert(kFixyDecideOracleCount == 11, "ceiling: fixy::decide::oracle:: re-exports exactly 11 oracles.  If "
                                            "you add/remove an oracle, update the constant AND the using-decl "
                                            "AND the same_*_v witness in the same edit.");

}  // namespace crucible::fixy::decide::oracle::self_test

namespace crucible::fixy::decide::oracle::self_test {

// Non-constant values keep the consteval fast path from masking an
// inline-body fault.
inline void runtime_smoke_test() {
    volatile int a = 6;
    volatile int b = 4;
    volatile bool sink = false;
    sink = ::crucible::fixy::decide::oracle::no_overflow_mul_oracle<int>(a, b);
    sink = ::crucible::fixy::decide::oracle::no_overflow_sum_oracle<int>(a, b);
    sink = ::crucible::fixy::decide::oracle::coprime_oracle<int>(a, b);
    sink = ::crucible::fixy::decide::oracle::is_power_of_two_le_oracle<int>(a, 64);
    int xs[] = {1, 2, 3, 4, 5};
    sink = ::crucible::fixy::decide::oracle::strictly_increasing_oracle<int>(std::span<const int>{xs});
    sink = ::crucible::fixy::decide::oracle::weakly_increasing_oracle<int>(std::span<const int>{xs});
    sink = ::crucible::fixy::decide::oracle::all_in_range_oracle<int>(std::span<const int>{xs}, 0, 10);
    bool bs[] = {true, true, false};
    sink = ::crucible::fixy::decide::oracle::conjunction_oracle(std::span<const bool>{bs});
    sink = ::crucible::fixy::decide::oracle::disjunction_oracle(std::span<const bool>{bs});
    volatile std::uint64_t v = 64;
    sink = ::crucible::fixy::decide::oracle::aligned_in_range_oracle(v, 0, 128, 16);
    (void)sink;
}

}  // namespace crucible::fixy::decide::oracle::self_test
