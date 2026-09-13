// The predicate catalog is re-exported under a second namespace.  This
// file proves that the re-exported name denotes the same symbol as the
// original and that calling it through the re-export still gives the
// original's answers.
//
// Every call below passes arguments the compiler cannot fold.  A test
// built only from static assertions would exercise the constant
// evaluator and miss a bug that appears only on the runtime path.

#include <crucible/Fixy.h>

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#ifndef CRUCIBLE_FIXY
#error "crucible/Fixy.h umbrella did not define CRUCIBLE_FIXY"
#endif

namespace fd = ::crucible::fixy::decide;
namespace cd = ::crucible::decide;

// The per-predicate identity witnesses are asserted where they are
// defined.  Naming two of them here additionally pins that the witness
// namespace survives a header restructure, so hiding it is a plain
// error rather than a silent loss of the identity guarantee.
static_assert(::crucible::fixy::decide::self_test::same_no_overflow_mul_v,
              "The identity witnesses must be reachable through the umbrella.");
static_assert(::crucible::fixy::decide::self_test::same_in_range_v,
              "The identity witnesses must be reachable through the umbrella.");

// These are the floor halves of floor-and-ceiling pairs.  Each exact
// ceiling sits beside the constant it pins, so these witnesses only
// have to catch the inverse direction: removal of an entry.
static_assert(::crucible::fixy::decide::self_test::kFixyDecidePredicateCount >= 23,
              "The predicate catalog lost an entry without the ceiling pin being "
              "updated alongside this floor.");
static_assert(::crucible::fixy::decide::self_test::kFixyDecideTypeCount >= 1,
              "The type catalog lost an entry without the ceiling pin being "
              "updated alongside this floor.");

// The same pair of witnesses over the oracle sub-namespace.
static_assert(::crucible::fixy::decide::oracle::self_test::same_no_overflow_mul_oracle_v,
              "The oracle sub-namespace must be reachable through the umbrella.");
static_assert(::crucible::fixy::decide::oracle::self_test::kFixyDecideOracleCount >= 11,
              "The oracle catalog lost an entry without the ceiling pin being "
              "updated alongside this floor.");

namespace {

[[noreturn]] void fail(const char* msg) {
    std::fprintf(stderr, "test_fixy_decide: %s\n", msg);
    std::abort();
}

void check(bool cond, const char* msg) {
    if (!cond) fail(msg);
}

// Scalar operands below are declared volatile so the compiler must
// read them back rather than fold the call away.  Operands passed as
// spans defeat folding on their own.

void smoke_no_overflow_arithmetic() {
    volatile int a = 100;
    volatile int b = 200;
    volatile int big_a = INT32_MAX;
    volatile int big_b = 2;
    check(fd::no_overflow_mul(a, b), "no_overflow_mul(100, 200)");
    check(!fd::no_overflow_mul(big_a, big_b), "no_overflow_mul(INT_MAX, 2)");
    check(fd::no_overflow_sum(a, b), "no_overflow_sum(100, 200)");
    check(!fd::no_overflow_sum(big_a, b), "no_overflow_sum(INT_MAX, 200)");
    volatile int shift_a = 1;
    volatile int shift_b = 5;
    check(fd::no_overflow_pow2_shift(shift_a, shift_b), "no_overflow_pow2_shift(1, 5)");
}

void smoke_range_ordering() {
    std::array<int, 5> sorted_strict = {1, 2, 3, 4, 5};
    std::array<int, 5> sorted_weak = {1, 2, 2, 4, 5};
    std::array<int, 5> unsorted = {1, 3, 2, 4, 5};
    std::array<int, 3> in_range_xs = {3, 5, 7};
    std::array<int, 3> out_range_xs = {3, 5, 15};

    check(fd::strictly_increasing(std::span<const int>{sorted_strict}), "strictly_increasing(sorted)");
    check(!fd::strictly_increasing(std::span<const int>{sorted_weak}), "strictly_increasing(weak) rejects");
    check(!fd::strictly_increasing(std::span<const int>{unsorted}), "strictly_increasing(unsorted) rejects");
    check(fd::weakly_increasing(std::span<const int>{sorted_weak}), "weakly_increasing(weak)");
    check(fd::weakly_increasing(std::span<const int>{sorted_strict}), "weakly_increasing(strict)");
    check(!fd::weakly_increasing(std::span<const int>{unsorted}), "weakly_increasing(unsorted) rejects");
    check(fd::all_in_range(std::span<const int>{in_range_xs}, 0, 10), "all_in_range([3,5,7], 0, 10)");
    check(!fd::all_in_range(std::span<const int>{out_range_xs}, 0, 10), "all_in_range([3,5,15], 0, 10) rejects");
}

void smoke_divisibility() {
    volatile int eight = 8;
    volatile int ten = 10;
    volatile int six = 6;
    volatile int hundred = 100;
    check(fd::is_power_of_two_le(eight, hundred), "is_power_of_two_le(8, 100)");
    check(!fd::is_power_of_two_le(six, hundred), "is_power_of_two_le(6, 100) rejects (not a power of two)");
    check(!fd::is_power_of_two_le(eight, six), "is_power_of_two_le(8, 6) rejects (exceeds bound)");
    // 8 and 9 are consecutive, so their only common divisor is 1,
    // whereas 8 and 10 share 2.
    volatile int nine = 9;
    check(fd::coprime(eight, nine), "coprime(8, 9)");
    check(!fd::coprime(eight, ten), "coprime(8, 10) rejects (gcd=2)");
    std::array<int, 2> factors = {2, 5};
    check(fd::factorization_eq(std::span<const int>{factors}, ten), "factorization_eq([2,5], 10)");
    std::array<int, 2> bad_factors = {2, 3};
    check(!fd::factorization_eq(std::span<const int>{bad_factors}, ten), "factorization_eq([2,3], 10) rejects");
}

void smoke_intervals() {
    using Iv = fd::Interval<int>;
    std::array<Iv, 2> disjoint = {Iv{0, 5}, Iv{10, 15}};
    std::array<Iv, 2> overlap = {Iv{0, 10}, Iv{5, 15}};
    check(fd::intervals_pairwise_disjoint(std::span<const Iv>{disjoint}),
          "intervals_pairwise_disjoint([0..5, 10..15])");
    check(!fd::intervals_pairwise_disjoint(std::span<const Iv>{overlap}),
          "intervals_pairwise_disjoint([0..10, 5..15]) rejects");
}

void smoke_boolean_folds() {
    std::array<bool, 3> all_true = {true, true, true};
    std::array<bool, 3> one_false = {true, false, true};
    std::array<bool, 3> all_false = {false, false, false};
    check(fd::conjunction(std::span<const bool>{all_true}), "conjunction([T,T,T])");
    check(!fd::conjunction(std::span<const bool>{one_false}), "conjunction([T,F,T]) rejects");
    check(fd::disjunction(std::span<const bool>{one_false}), "disjunction([T,F,T])");
    check(!fd::disjunction(std::span<const bool>{all_false}), "disjunction([F,F,F]) rejects");
    volatile bool ante = true;
    volatile bool cons = true;
    check(fd::implies(ante, cons), "implies(T, T)");
    cons = false;
    check(!fd::implies(ante, cons), "implies(T, F) rejects");
    ante = false;
    check(fd::implies(ante, cons), "implies(F, _)");
}

void smoke_scalar_range() {
    volatile int x = 5;
    volatile int zero = 0;
    volatile int neg = -7;
    check(fd::in_range(x, 0, 10), "in_range(5, 0, 10)");
    check(!fd::in_range(x, 6, 10), "in_range(5, 6, 10) rejects");
    check(fd::positive(x), "positive(5)");
    check(!fd::positive(zero), "positive(0) rejects");
    check(!fd::positive(neg), "positive(-7) rejects");
    check(fd::non_negative(x), "non_negative(5)");
    check(fd::non_negative(zero), "non_negative(0)");
    check(!fd::non_negative(neg), "non_negative(-7) rejects");
    volatile std::uint64_t aligned = 64;
    check(fd::aligned_in_range(aligned, /*lo=*/0, /*hi=*/128, /*alignment=*/8), "aligned_in_range(64, 0, 128, 8)");
    volatile std::uint64_t unaligned = 65;
    check(!fd::aligned_in_range(unaligned, 0, 128, 8), "aligned_in_range(65, 0, 128, 8) rejects (not aligned)");
}

void smoke_pointer_span() {
    std::array<int, 4> data = {1, 2, 3, 4};
    int* ptr = data.data();
    check(fd::valid_span(int{4}, static_cast<const void*>(ptr)), "valid_span(4, ptr)");
    check(!fd::valid_span(int{4}, static_cast<const void*>(nullptr)), "valid_span(4, nullptr) rejects");
    check(fd::valid_span(int{0}, static_cast<const void*>(nullptr)), "valid_span(0, nullptr) accepts (empty span)");
    volatile int nz = 7;
    volatile int z = 0;
    check(fd::is_non_zero(nz), "is_non_zero(7)");
    check(!fd::is_non_zero(z), "is_non_zero(0) rejects");
}

void smoke_hash_fmix() {
    // The predicate only records the bijection claim at the site that
    // relies on it.  The claim itself is discharged by a fuzzer, not
    // here, so these cases check the guard and not the theorem.
    volatile std::uint64_t seed_nz = 0xcbf29ce484222325ULL;
    volatile std::uint64_t mix_nz = 0x9876;
    volatile std::uint64_t zero = 0;
    check(fd::fmix_preserves_non_zero(seed_nz, mix_nz), "fmix_preserves_non_zero(nz, nz)");
    check(!fd::fmix_preserves_non_zero(zero, mix_nz), "fmix_preserves_non_zero(0, nz) rejects");
    check(!fd::fmix_preserves_non_zero(seed_nz, zero), "fmix_preserves_non_zero(nz, 0) rejects");
}

}  // namespace

int main() {
    smoke_no_overflow_arithmetic();
    smoke_range_ordering();
    smoke_divisibility();
    smoke_intervals();
    smoke_boolean_folds();
    smoke_scalar_range();
    smoke_pointer_span();
    smoke_hash_fmix();

    // Calling the in-header smoke tests gives anything added to them
    // execution coverage here without this function having to grow.
    ::crucible::fixy::decide::self_test::runtime_smoke_test();
    ::crucible::fixy::decide::oracle::self_test::runtime_smoke_test();

    return 0;
}
