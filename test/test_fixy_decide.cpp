// ═══════════════════════════════════════════════════════════════════
// test_fixy_decide — FIXY-U-011 sentinel TU
//
// Verifies that the entire `crucible::decide::` predicate catalog
// reaches through `<crucible/Fixy.h>` under fixy::decide::, that
// every predicate's substrate symbol is invariant under the
// re-export (same_*_v witnesses ride fixy/Decide.h itself), and
// that each predicate evaluates correctly at runtime through the
// fixy:: path.
//
// Closes #1726 (FIXY-U-011): "fixy::decide:: new namespace —
// 22 named VC predicates + sentinel TU + runtime smoke".
//
// Trust boundary:
//   safety/Decide.h owns predicate semantics + correctness proofs
//                   (per CONTRACT-* migration discipline).
//   fixy/Decide.h owns the substrate-identity witnesses (one
//                 same_*_v per predicate, embedded in self_test::).
//   THIS TU owns the end-to-end runtime smoke through the umbrella
//           — each predicate exercised with non-constant runtime
//           arguments to defeat consteval folding.
//
// The runtime smoke is necessary per feedback_algebra_runtime_smoke
// _test_discipline.md: pure static_assert tests mask consteval/
// SFINAE bugs that surface only when the predicate is called with
// non-literal arguments.
// ═══════════════════════════════════════════════════════════════════

#include <crucible/Fixy.h>

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#ifndef CRUCIBLE_FIXY
#  error "crucible/Fixy.h umbrella did not define CRUCIBLE_FIXY"
#endif

namespace fd = ::crucible::fixy::decide;
namespace cd = ::crucible::decide;

// ═════════════════════════════════════════════════════════════════════
// Compile-time reachability — every predicate's same_*_v witness is
// already asserted inside fixy/Decide.h.  We additionally pin the
// witness namespace's reach through the umbrella here so a future
// header-restructure that hides self_test:: produces a TU-level
// error rather than a silent regression of the substrate-identity
// guarantee.
// ═════════════════════════════════════════════════════════════════════

static_assert(::crucible::fixy::decide::self_test::same_no_overflow_mul_v,
    "umbrella reach: fixy::decide::self_test::same_no_overflow_mul_v "
    "must be reachable through <crucible/Fixy.h>.");
static_assert(::crucible::fixy::decide::self_test::same_in_range_v,
    "umbrella reach: fixy::decide::self_test::same_in_range_v must be "
    "reachable through <crucible/Fixy.h>.");
// FIXY-U-128 floor witnesses (the EXACT ceiling pins `== 23` / `== 1`
// live in fixy/Decide.h colocated with the source-of-truth constants;
// these floors catch accidental REMOVAL of a catalog entry).
static_assert(::crucible::fixy::decide::self_test::kFixyDecidePredicateCount >= 23,
    "floor: fixy::decide:: predicate cardinality regressed below 23 — "
    "a predicate was removed without updating both Decide.h's "
    "colocated ceiling pin AND this floor witness.");
static_assert(::crucible::fixy::decide::self_test::kFixyDecideTypeCount >= 1,
    "floor: fixy::decide:: type cardinality regressed below 1 — "
    "Interval<T> was removed without updating both Decide.h's "
    "colocated ceiling pin AND this floor witness.");

// FIXY-V-178 — oracle surface reach + floor.  The EXACT ceiling pin
// (`== 11`) lives in fixy/Decide.h colocated with the source-of-truth
// constant; this floor catches accidental REMOVAL of an oracle
// re-export, and the same_*_v reach asserts the oracle sub-namespace
// is visible through the <crucible/Fixy.h> umbrella.
static_assert(::crucible::fixy::decide::oracle::self_test::same_no_overflow_mul_oracle_v,
    "umbrella reach: fixy::decide::oracle::self_test::same_no_overflow_mul_oracle_v "
    "must be reachable through <crucible/Fixy.h>.");
static_assert(::crucible::fixy::decide::oracle::self_test::kFixyDecideOracleCount >= 11,
    "floor: fixy::decide::oracle:: cardinality regressed below 11 — an "
    "oracle re-export was removed without updating both Decide.h's "
    "colocated ceiling pin AND this floor witness.");

// ═════════════════════════════════════════════════════════════════════
// Runtime smoke — every predicate evaluated with NON-CONSTANT args
// to defeat consteval folding.  Volatile sinks force evaluation at
// runtime; the runtime path goes through the same fixy::decide::
// surface that production callers would.
// ═════════════════════════════════════════════════════════════════════

namespace {

[[noreturn]] void fail(const char* msg) {
    std::fprintf(stderr, "test_fixy_decide: %s\n", msg);
    std::abort();
}

void check(bool cond, const char* msg) {
    if (!cond) fail(msg);
}

// Each test function exercises one predicate (or a tight cluster) with
// runtime args.  The check() calls assert known-correct outcomes; if
// the fixy:: path were silently bound to a different substrate symbol,
// the outcome would diverge from the substrate's documented semantics.

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
    check(fd::no_overflow_pow2_shift(shift_a, shift_b),
          "no_overflow_pow2_shift(1, 5)");
}

void smoke_range_ordering() {
    std::array<int, 5> sorted_strict = {1, 2, 3, 4, 5};
    std::array<int, 5> sorted_weak   = {1, 2, 2, 4, 5};
    std::array<int, 5> unsorted      = {1, 3, 2, 4, 5};
    std::array<int, 3> in_range_xs   = {3, 5, 7};
    std::array<int, 3> out_range_xs  = {3, 5, 15};

    check(fd::strictly_increasing(std::span<const int>{sorted_strict}),
          "strictly_increasing(sorted)");
    check(!fd::strictly_increasing(std::span<const int>{sorted_weak}),
          "strictly_increasing(weak) rejects");
    check(!fd::strictly_increasing(std::span<const int>{unsorted}),
          "strictly_increasing(unsorted) rejects");
    check(fd::weakly_increasing(std::span<const int>{sorted_weak}),
          "weakly_increasing(weak)");
    check(fd::weakly_increasing(std::span<const int>{sorted_strict}),
          "weakly_increasing(strict)");
    check(!fd::weakly_increasing(std::span<const int>{unsorted}),
          "weakly_increasing(unsorted) rejects");
    check(fd::all_in_range(std::span<const int>{in_range_xs}, 0, 10),
          "all_in_range([3,5,7], 0, 10)");
    check(!fd::all_in_range(std::span<const int>{out_range_xs}, 0, 10),
          "all_in_range([3,5,15], 0, 10) rejects");
}

void smoke_divisibility() {
    volatile int eight = 8;
    volatile int ten = 10;
    volatile int six = 6;
    volatile int hundred = 100;
    check(fd::is_power_of_two_le(eight, hundred),
          "is_power_of_two_le(8, 100)");
    check(!fd::is_power_of_two_le(six, hundred),
          "is_power_of_two_le(6, 100) rejects (not a power of two)");
    check(!fd::is_power_of_two_le(eight, six),
          "is_power_of_two_le(8, 6) rejects (exceeds bound)");
    // coprime: gcd(8, 9) = 1
    volatile int nine = 9;
    check(fd::coprime(eight, nine), "coprime(8, 9)");
    check(!fd::coprime(eight, ten), "coprime(8, 10) rejects (gcd=2)");
    // factorization_eq: 2 × 5 = 10
    std::array<int, 2> factors = {2, 5};
    check(fd::factorization_eq(std::span<const int>{factors}, ten),
          "factorization_eq([2,5], 10)");
    std::array<int, 2> bad_factors = {2, 3};
    check(!fd::factorization_eq(std::span<const int>{bad_factors}, ten),
          "factorization_eq([2,3], 10) rejects");
}

void smoke_intervals() {
    using Iv = fd::Interval<int>;
    std::array<Iv, 2> disjoint = {Iv{0, 5}, Iv{10, 15}};
    std::array<Iv, 2> overlap  = {Iv{0, 10}, Iv{5, 15}};
    check(fd::intervals_pairwise_disjoint(
              std::span<const Iv>{disjoint}),
          "intervals_pairwise_disjoint([0..5, 10..15])");
    check(!fd::intervals_pairwise_disjoint(
              std::span<const Iv>{overlap}),
          "intervals_pairwise_disjoint([0..10, 5..15]) rejects");
}

void smoke_boolean_folds() {
    std::array<bool, 3> all_true  = {true, true, true};
    std::array<bool, 3> one_false = {true, false, true};
    std::array<bool, 3> all_false = {false, false, false};
    check(fd::conjunction(std::span<const bool>{all_true}),
          "conjunction([T,T,T])");
    check(!fd::conjunction(std::span<const bool>{one_false}),
          "conjunction([T,F,T]) rejects");
    check(fd::disjunction(std::span<const bool>{one_false}),
          "disjunction([T,F,T])");
    check(!fd::disjunction(std::span<const bool>{all_false}),
          "disjunction([F,F,F]) rejects");
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
    check(fd::aligned_in_range(aligned, /*lo=*/0, /*hi=*/128, /*alignment=*/8),
          "aligned_in_range(64, 0, 128, 8)");
    volatile std::uint64_t unaligned = 65;
    check(!fd::aligned_in_range(unaligned, 0, 128, 8),
          "aligned_in_range(65, 0, 128, 8) rejects (not aligned)");
}

void smoke_pointer_span() {
    std::array<int, 4> data = {1, 2, 3, 4};
    int* ptr = data.data();
    check(fd::valid_span(int{4}, static_cast<const void*>(ptr)),
          "valid_span(4, ptr)");
    check(!fd::valid_span(int{4}, static_cast<const void*>(nullptr)),
          "valid_span(4, nullptr) rejects");
    check(fd::valid_span(int{0}, static_cast<const void*>(nullptr)),
          "valid_span(0, nullptr) accepts (empty span)");
    volatile int nz = 7;
    volatile int z = 0;
    check(fd::is_non_zero(nz), "is_non_zero(7)");
    check(!fd::is_non_zero(z), "is_non_zero(0) rejects");
}

void smoke_hash_fmix() {
    // fmix_preserves_non_zero(seed, mix_output) — both non-zero ⇒ true.
    // The predicate pins the fmix64 bijection at the citation site; the
    // theorem itself is proved by the bench_fmix_bijection fuzzer.
    volatile std::uint64_t seed_nz = 0xcbf29ce484222325ULL;
    volatile std::uint64_t mix_nz  = 0x9876;
    volatile std::uint64_t zero    = 0;
    check(fd::fmix_preserves_non_zero(seed_nz, mix_nz),
          "fmix_preserves_non_zero(nz, nz)");
    check(!fd::fmix_preserves_non_zero(zero, mix_nz),
          "fmix_preserves_non_zero(0, nz) rejects");
    check(!fd::fmix_preserves_non_zero(seed_nz, zero),
          "fmix_preserves_non_zero(nz, 0) rejects");
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

    // Also exercise the in-header runtime_smoke_test() so any future
    // additions to that block get TU-level execution coverage even
    // without expanding this main().
    ::crucible::fixy::decide::self_test::runtime_smoke_test();

    // FIXY-V-178 — exercise the oracle surface's runtime smoke too.
    ::crucible::fixy::decide::oracle::self_test::runtime_smoke_test();

    return 0;
}
