// SPDX-License-Identifier: Apache-2.0
// crucible — safety/Decide.h
//
// CRUCIBLE_PRE / CRUCIBLE_POST predicate library.  Verification-condition
// (VC) discharge primitives for production preconditions, postconditions,
// and invariants.
//
// ───────────────────────────────────────────────────────────────────
// WHY THIS HEADER EXISTS
// ───────────────────────────────────────────────────────────────────
//
// Same predicate is rewritten at every call site that needs it:
//
//   pre (a > 0 && b > 0 && a < INT64_MAX / b)           — call site 1
//   pre (lhs > 0 && rhs > 0 && lhs < SIZE_MAX / rhs)    — call site 2
//   pre (count > 0 && stride > 0 && count <= INT_MAX / stride)
//                                                       — call site 3
//
// Each instance is the same VC discharge — "multiplication of these
// two operands does not overflow" — written three times with three
// different operand names and three different result types.  Three
// places to drift, three places to bug-hunt, three places to update
// when the saturation policy changes.
//
// crucible::decide::* factors the predicate into one constexpr
// function with a name:
//
//   pre (decide::no_overflow_mul(a, b))                — call site 1
//   pre (decide::no_overflow_mul(lhs, rhs))            — call site 2
//   pre (decide::no_overflow_mul(count, stride))       — call site 3
//
// One source of truth.  One place to fuzz against a slow oracle
// (CONTRACT-090 + CONTRACT-091).  One place to grep for cite count
// (CONTRACT-125 / CONTRACT-126 6-month catalog-trim audit).  No
// drift.
//
// ───────────────────────────────────────────────────────────────────
// DESIGN PRINCIPLES
// ───────────────────────────────────────────────────────────────────
//
// Every Decide procedure is:
//
//   1. `[[nodiscard, gnu::const]] constexpr T noexcept` —
//      pure function of arguments only.  Safe at compile time and
//      runtime.  Optimizer may CSE / hoist / fold without restraint.
//
//   2. Predicate-shaped — returns `bool` (or a thin `std::optional`-
//      like discriminator).  Suitable for direct use inside
//      CRUCIBLE_PRE / CRUCIBLE_POST / static_assert.
//
//   3. Total — defined for all input values in its declared types.
//      No hidden preconditions; no UB on adversarial input.
//
//   4. Cite-tracked — every production call site is logged in the
//      header doc-comment so 6-month audits can identify procedures
//      with no cites and trim them (CONTRACT-126).
//
// Procedures that compose other procedures (e.g. `aligned_in_range`)
// live near their building blocks for grep-locality.
//
// ───────────────────────────────────────────────────────────────────
// COST MODEL
// ───────────────────────────────────────────────────────────────────
//
// Predicate evaluation only.  No allocation, no I/O, no side effect.
// All builtins fold under constant args, so consteval evaluation is
// effectively free.  Runtime cost is a handful of compare + branch
// instructions per invocation — well under the 1-2 ns per CRUCIBLE_PRE
// call-site overhead budget.
//
// When wrapped in CRUCIBLE_PRE under -DNDEBUG, the predicate
// evaluation itself is folded out via the [[assume]] hint — see
// safety/Pre.h §52-66 cost-model claim and test/test_pre_post_cost.cpp
// (CONTRACT-008) for the proof.
//
// ───────────────────────────────────────────────────────────────────
// USAGE PATTERN
// ───────────────────────────────────────────────────────────────────
//
//   #include <crucible/safety/Decide.h>
//   #include <crucible/safety/Pre.h>
//
//   constexpr uint64_t safe_mul_u64(uint64_t a, uint64_t b) noexcept {
//       CRUCIBLE_PRE(crucible::decide::no_overflow_mul(a, b));
//       return a * b;
//   }
//
//   // At consteval, a planted overflow fails compilation:
//   //   static_assert(safe_mul_u64(UINT64_MAX, 2) == ...);
//   //                 ↑ rejected: "non-constant condition"
//
//   // At runtime under !NDEBUG, contract_failed prints the predicate
//   // expression "crucible::decide::no_overflow_mul(a, b)" along
//   // with file/line/function and aborts.
//
// ───────────────────────────────────────────────────────────────────
// CRUCIBLE AXIOMS
// ───────────────────────────────────────────────────────────────────
//
//   InitSafe  — predicate args are bare values; same idioms as a
//               plain `if (...)` check.
//   TypeSafe  — every procedure is template-constrained by `std::
//               integral` or stronger; ill-typed instantiations are
//               compile errors.
//   DetSafe   — gnu::const + noexcept + no global state; identical
//               args produce identical results across runs and
//               across vendors.

#pragma once

#include <concepts>
#include <cstddef>
#include <limits>
#include <span>
#include <type_traits>

namespace crucible::decide {

// ─── no_overflow_mul ───────────────────────────────────────────────
//
// Returns true iff `a * b` does not overflow type T.  T may be any
// signed or unsigned integral type; the sign behavior of overflow is
// detected via `__builtin_mul_overflow`, which handles all integer
// widths and signedness consistently.
//
// SEMANTIC NOTE
// -------------
// "Overflow" here means the mathematical product cannot be
// represented in T.  For unsigned T, this is `mathematical(a*b) >
// std::numeric_limits<T>::max()`.  For signed T, this is
// `mathematical(a*b) < std::numeric_limits<T>::min()` OR
// `mathematical(a*b) > std::numeric_limits<T>::max()`.  Both extremes
// return false from this procedure.
//
// The result `T r` is computed but discarded — the only signal
// callers care about is the overflow flag.  The optimizer eliminates
// the dead store under -O1 and higher.
//
// PRODUCTION CITES (update on adoption per CONTRACT-125)
// ------------------------------------------------------
//   (none yet — first production migration lands with CONTRACT-105)
//
// ANTI-PATTERNS (review-rejected)
// -------------------------------
//   * `pre (a < INT_MAX / b)` — wrong for `b == 0` (UB) and for
//     signed types where INT_MIN / (-1) traps.  Always cite this
//     procedure instead.
//   * `pre (a * b > 0)` — wrong for many overflow patterns
//     (e.g. UINT_MAX * 2 wraps to UINT_MAX - 1, still positive).
template <std::integral T>
[[nodiscard, gnu::const]]
constexpr bool no_overflow_mul(T a, T b) noexcept {
    T r{};
    return !__builtin_mul_overflow(a, b, &r);
}

// ─── no_overflow_sum ───────────────────────────────────────────────
//
// Returns true iff `a + b` does not overflow type T.  T may be any
// signed or unsigned integral type; signed overflow at BOTH extremes
// (INT_MAX + 1 → wrap to INT_MIN, INT_MIN + (-1) → wrap to INT_MAX)
// is detected via `__builtin_add_overflow`.  The procedure is total:
// every (a, b) input pair returns a defined bool, no UB on adversarial
// values.
//
// SEMANTIC NOTE
// -------------
// For unsigned T: returns false iff `a + b > std::numeric_limits<T>::
// max()`.  For signed T: returns false iff the mathematical sum lies
// outside [std::numeric_limits<T>::min(), std::numeric_limits<T>::max()].
// The carry-out / sign-bit detection inside __builtin_add_overflow is
// width- and signedness-aware.
//
// USAGE PATTERN
// -------------
//
//   constexpr int32_t safe_add_i32(int32_t a, int32_t b) noexcept {
//       CRUCIBLE_PRE(crucible::decide::no_overflow_sum(a, b));
//       return a + b;
//   }
//
//   // Consteval witness fires CRUCIBLE_PRE's __builtin_trap():
//   //   static_assert(safe_add_i32(INT32_MAX, 1) == 0);
//   //                 ↑ rejected: "non-constant condition"
//
// PRODUCTION CITES (update on adoption per CONTRACT-125)
// ------------------------------------------------------
//   (none yet — first migration batch lands with CONTRACT-104:
//    StorageNbytes.h and MerkleDag.h max_offset/min_offset accumulation)
//
// ANTI-PATTERNS (review-rejected)
// -------------------------------
//   * `pre (a + b >= a)` — wrong for signed overflow (UB at the +,
//     not after) AND for negative b on unsigned types.
//   * `pre (a < INT_MAX - b)` — wrong for `b == INT_MIN` on signed T
//     (`INT_MAX - INT_MIN` overflows itself).
//   * `pre (a < 0 == (b < 0) || (a + b) < 0 == ...)` — error-prone
//     XOR sign tricks; always cite this procedure instead.
template <std::integral T>
[[nodiscard, gnu::const]]
constexpr bool no_overflow_sum(T a, T b) noexcept {
    T r{};
    return !__builtin_add_overflow(a, b, &r);
}

// ─── no_overflow_pow2_shift ────────────────────────────────────────
//
// Returns true iff `a << b` is well-defined AND the mathematical
// result `a * 2^b` is representable in type T.  Handles every UB
// class the C++ left-shift operator imposes:
//
//   1. b < 0  (signed T only — "negative shift count")
//   2. b >= bitwidth(T)  ("shift count out of range")
//   3. a < 0  (signed T only — "left-shift of negative value")
//   4. mathematical(a * 2^b) > std::numeric_limits<T>::max()
//      ("shifts information into the sign bit" / unsigned wrap)
//
// The four cases together are the complete failure surface of
// `a << b` per [expr.shift]; classes 1-3 are UB at the operator
// itself, class 4 is wraparound (defined for unsigned, UB for signed).
//
// SEMANTIC NOTE
// -------------
// Equivalent reading: `a << b == a * pow(2, b)`.  This procedure is
// the saturation predicate for that multiplication, but specialized
// because C++ shift cannot reuse __builtin_mul_overflow directly —
// classes 1, 2, 3 must be detected BEFORE evaluating any expression
// involving `<<`.  The implementation uses arithmetic comparison
// against `MAX >> b`, which is well-defined as long as `b in [0, W)`
// (verified first), and avoids constructing `T{1} << b` (would be
// UB at b == W-1 for signed T).
//
// The shift count parameter is the same type T as the value, matching
// the no_overflow_mul / no_overflow_sum signature shape.  Production
// callers that have a non-T shift count should cast or use a
// per-call-site Refined<bounded_below<0> ∧ bounded_above<W>, int>
// aliased predicate.
//
// USAGE PATTERN
// -------------
//
//   constexpr uint32_t safe_shl_u32(uint32_t a, uint32_t b) noexcept {
//       CRUCIBLE_PRE(crucible::decide::no_overflow_pow2_shift(a, b));
//       return a << b;
//   }
//
//   // Both consteval witnesses fire CRUCIBLE_PRE's __builtin_trap():
//   //   static_assert(safe_shl_u32(1u, 32u) == 0);  // shift count UB
//   //   static_assert(safe_shl_u32(0xFFFFFFFFu, 1u) == 0);  // wrap
//
// PRODUCTION CITES (update on adoption per CONTRACT-125)
// ------------------------------------------------------
//   (none yet — first migration batch lands with CONTRACT-109:
//    RecipePool / ExprPool / SwissCtrl power-of-two arithmetic)
//
// ANTI-PATTERNS (review-rejected)
// -------------------------------
//   * `pre (b < 64)` — width-hardcoded; wrong for any T other than
//     uint64_t / int64_t.  Always reference `bitwidth(T)`.
//   * `pre (a << b > a)` — circular: uses the very operation we're
//     guarding.  UB if b >= W; the comparison is meaningless on UB.
//   * `pre (b < sizeof(T) * 8)` — misses the negative-shift case for
//     signed b (b == -1 silently passes a uint8_t-cast bug).
//   * `pre ((a >> (W - 1 - b)) == 0)` — fragile around boundaries
//     (`W - 1 - b` can underflow for hostile b).  Always cite this
//     procedure instead.
template <std::integral T>
[[nodiscard, gnu::const]]
constexpr bool no_overflow_pow2_shift(T a, T b) noexcept {
    constexpr T W = static_cast<T>(sizeof(T) * 8);
    if constexpr (std::is_signed_v<T>) {
        if (b < T{0} || b >= W) {
            return false;
        }
        if (a < T{0}) {
            return false;
        }
        return a <= (std::numeric_limits<T>::max() >> b);
    } else {
        if (b >= W) {
            return false;
        }
        return a <= (std::numeric_limits<T>::max() >> b);
    }
}

// ─── all_in_range ──────────────────────────────────────────────────
//
// Returns true iff every element of `xs` satisfies `lo <= x && x <= hi`.
// Empty span returns true vacuously (a property of universally-
// quantified predicates over empty domains — `∀x ∈ ∅. P(x)` is `true`).
//
// Bridges from elementwise to range-quantified predicates.  The
// previous Decide procedures were pointwise binary (a, b → bool);
// this one is the canonical span-quantified shape:
//   `(span<T>, T, T) → bool` — quantifies a binary predicate over
// every element of a sequence.  Production code that hand-rolls
// "for every i in [0, n): assert(arr[i] >= lo && arr[i] <= hi)"
// has TWO bug surfaces (the loop index AND the comparison) that
// this procedure flattens to one.
//
// SEMANTIC NOTE
// -------------
// `lo > hi` is permitted: the predicate returns `xs.empty()` in
// that case (vacuous truth on empty span; rejection on any element
// otherwise).  This is different from a precondition `pre(lo <= hi)`
// — the predicate is intentionally TOTAL over (xs, lo, hi) per
// CONTRACT-020 design principle #3.  Callers that want a stricter
// "valid range bounds" check should compose with a separate cite.
//
// USAGE PATTERN
// -------------
//
//   // CONTRACT-102 production usage shape: SymbolTable indexed
//   // access where every SlotId must be < num_slots.
//   constexpr bool all_slots_in_pool(std::span<const SlotId> ids,
//                                    SlotId lo,
//                                    SlotId hi) noexcept {
//       CRUCIBLE_PRE(crucible::decide::all_in_range(ids, lo, hi));
//       return true;
//   }
//
//   // At consteval, an out-of-range planted element fails compilation:
//   //   constexpr SlotId arr[] = {1, 2, 99};  // 99 > MAX
//   //   static_assert(all_slots_in_pool(arr, 0, 50));
//   //                 ↑ rejected: "non-constant condition"
//
// PRODUCTION CITES (update on adoption per CONTRACT-125)
// ------------------------------------------------------
//   (none yet — first migration batch lands with CONTRACT-102:
//    SymbolTable + TraceGraph indexed access bounds chains)
//
// ANTI-PATTERNS (review-rejected)
// -------------------------------
//   * `pre (n > 0 && arr[0] >= lo && arr[n-1] <= hi)` — only checks
//     endpoints; misses an out-of-range element in the middle.
//   * `pre (lo <= *std::min_element(begin, end))` — TWO scans over
//     the data + double-bounds-check overhead; cite this procedure
//     for ONE pass.
//   * Loop hand-rolled at every call site — three places to drift,
//     three places to forget the `<=` vs `<` boundary.
template <std::integral T>
[[nodiscard, gnu::pure]]
constexpr bool all_in_range(std::span<const T> xs, T lo, T hi) noexcept {
    for (T const& x : xs) {
        if (x < lo || x > hi) {
            return false;
        }
    }
    return true;
}

// ─── strictly_increasing ───────────────────────────────────────────
//
// Returns true iff `xs[i-1] < xs[i]` for every consecutive pair
// (i ∈ [1, xs.size())).  Empty span and single-element span return
// true vacuously — there are no consecutive pairs to violate the
// ordering, so the universally-quantified predicate is trivially
// satisfied.
//
// The CONSECUTIVE-PAIR-QUANTIFIED shape — distinct from the per-
// element shape of all_in_range.  The procedure walks adjacent
// (i-1, i) pairs and enforces strict less-than.  Production usage:
// step_id sequences (Cipher event log per CONTRACT-107), monotonic
// timestamps, generation counters, sorted index arrays where
// duplicates are forbidden.
//
// SEMANTIC NOTE
// -------------
// "Strict" means `<`, not `<=`.  Equal consecutive elements
// (`xs[i-1] == xs[i]`) FAIL the predicate.  This is the bug class
// where a counter stalls on the same value across two reads — a
// known failure mode in event-sourced systems where a duplicate
// record can be persisted without advancing the sequence number.
// For the `<=` shape (duplicates allowed) cite weakly_increasing
// (CONTRACT-042) instead.
//
// Vacuous truth: ∀i ∈ [1, n). P(i)  is true when n < 2.
//
// USAGE PATTERN
// -------------
//
//   // CONTRACT-107 production usage shape: Cipher::store enforces
//   // strictly-increasing step_id across persisted events.
//   constexpr bool valid_step_sequence(std::span<const uint64_t> ids) noexcept {
//       CRUCIBLE_PRE(crucible::decide::strictly_increasing(ids));
//       return true;
//   }
//
//   // At consteval, planted equal-pair fails compilation:
//   //   constexpr uint64_t arr[] = {1, 2, 2, 3};  // i=2 violation
//   //   static_assert(valid_step_sequence(arr));
//   //                 ↑ rejected: "non-constant condition"
//
// PRODUCTION CITES (update on adoption per CONTRACT-125)
// ------------------------------------------------------
//   (none yet — first migration batch lands with CONTRACT-107:
//    Cipher::store step_id sequence + CONTRACT-114 Transaction
//    count_ / Vigil step_ Monotonic discipline)
//
// ANTI-PATTERNS (review-rejected)
// -------------------------------
//   * `pre (xs.front() < xs.back())` — endpoint-only; misses any
//     non-monotonic interior pair.
//   * `pre (std::is_sorted(begin, end))` — uses `<=` semantics
//     (allows duplicates); a monotonically-stalling counter would
//     silently pass.
//   * `pre (xs[i] > xs[i-1])` checked only at one chosen i —
//     ad-hoc point check, not a quantified property.
template <std::integral T>
[[nodiscard, gnu::pure]]
constexpr bool strictly_increasing(std::span<const T> xs) noexcept {
    for (std::size_t i = 1; i < xs.size(); ++i) {
        if (!(xs[i - 1] < xs[i])) {
            return false;
        }
    }
    return true;
}

// ─── weakly_increasing ─────────────────────────────────────────────
//
// Returns true iff `xs[i-1] <= xs[i]` for every consecutive pair
// (i ∈ [1, xs.size())).  Empty span and single-element span return
// true vacuously — same vacuous-truth treatment as strictly_increasing.
//
// The `<=` shape: duplicates ARE permitted between consecutive
// elements.  Counterpart to strictly_increasing for production sites
// where stalling is acceptable but regression is not — e.g. file
// offsets where the same offset may legitimately repeat (multiple
// records at the same starting address with explicit length disambig),
// or histogram bin upper bounds where touching the next bin's low
// edge is the canonical transition.
//
// SEMANTIC NOTE
// -------------
// "Weak" means `<=`, not `<`.  Equal consecutive elements PASS the
// predicate.  Strict regression (`xs[i-1] > xs[i]`) FAILS.  This is
// the std::is_sorted's exact semantics — but giving it a named home
// in Decide consolidates the discharge across production sites and
// makes the contrast with strictly_increasing visible at the call
// site (review reads `decide::weakly_increasing(xs)` and knows the
// caller chose to accept duplicates intentionally).
//
// The strict-vs-weak choice is load-bearing for event-sourced systems
// (CONTRACT-107).  Cipher::store demands strictly_increasing because
// duplicate step_id breaks idempotence.  StorageSlot::offsets across
// concatenated tensors demands weakly_increasing because two adjacent
// zero-length records share the same offset.  The wrong choice
// silently corrupts replay; the right choice is enforced by cite.
//
// Vacuous truth: ∀i ∈ [1, n). P(i)  is true when n < 2.
//
// USAGE PATTERN
// -------------
//
//   // CONTRACT-110 production usage shape: TraceGraph edge offsets
//   // are weakly increasing (zero-length edges legitimately share
//   // their predecessor's terminal offset).
//   constexpr bool valid_edge_offsets(std::span<const uint32_t> offs) noexcept {
//       CRUCIBLE_PRE(crucible::decide::weakly_increasing(offs));
//       return true;
//   }
//
//   // At consteval, planted regression fails compilation:
//   //   constexpr uint32_t arr[] = {0, 5, 3, 7};  // 5 > 3 regression
//   //   static_assert(valid_edge_offsets(arr));
//   //                 ↑ rejected: "non-constant condition"
//
//   // Equal pair PASSES — duplicates allowed:
//   //   constexpr uint32_t arr[] = {0, 5, 5, 7};
//   //   static_assert(valid_edge_offsets(arr));   // ok
//
// PRODUCTION CITES (update on adoption per CONTRACT-125)
// ------------------------------------------------------
//   (none yet — first migration batch lands with CONTRACT-110:
//    TraceGraph CSR row-pointer offsets + StorageSlot offset chains)
//
// CONTRAST WITH strictly_increasing (CONTRACT-041)
// ------------------------------------------------
//   * Same shape, same vacuous-truth treatment.
//   * Differs only in `<=` vs `<` at the consecutive-pair test.
//   * Choice between them is a SEMANTIC DECISION about whether
//     duplicates are admissible.  Reviewers question every cite of
//     weakly_increasing where strictly_increasing would also satisfy
//     the production data — the looser predicate must be a deliberate
//     choice motivated by an admissible-duplicate scenario.
//
// ANTI-PATTERNS (review-rejected)
// -------------------------------
//   * `pre (xs.front() <= xs.back())` — endpoint-only; misses any
//     interior strict regression.
//   * `pre (xs[i] >= xs[i-1])` checked only at one chosen i —
//     ad-hoc point check, not a quantified property.
//   * Citing `strictly_increasing` where duplicates are admissible —
//     forces production code to manually filter duplicates before
//     the contract check, defeating the predicate's purpose.
template <std::integral T>
[[nodiscard, gnu::pure]]
constexpr bool weakly_increasing(std::span<const T> xs) noexcept {
    for (std::size_t i = 1; i < xs.size(); ++i) {
        if (xs[i - 1] > xs[i]) {
            return false;
        }
    }
    return true;
}

// ─── is_power_of_two_le ────────────────────────────────────────────
//
// Returns true iff `x` is a positive power of two AND `x <= bound`.
// Equivalently: x ∈ {1, 2, 4, 8, ...} ∩ [1, bound].  Rejects zero,
// rejects negatives (on signed T), rejects values exceeding bound,
// rejects values within bound that have more than one set bit.
//
// The conjunction structure — "power of two" AND "bounded above" —
// is the canonical capacity-validation shape across hash tables
// (RecipePool, ExprPool, SwissCtrl), bitmask-shaped masks (Cyclic
// counters, ring sizes), aligned allocator tiers, and SIMD lane
// counts.  Production code that hand-rolls "x is a power of two
// at most K" has TWO bug surfaces (the bit test AND the comparison)
// and HISTORICALLY ships with one or the other half-correct.
//
// SEMANTIC NOTE
// -------------
// Zero is REJECTED.  This matches std::has_single_bit (C++20) and
// std::bit_floor / std::bit_ceil semantics: 0 is not a power of two.
// Some legacy hardware conventions ("power of two of 2^∞") treat
// 0 as power-of-two; safety-critical code must reject zero here
// because downstream code that uses `mask = x - 1` would silently
// produce mask = UINT_MAX for x = 0, which would index out-of-bounds.
//
// Negative values on signed T are REJECTED.  Two's-complement
// representation guarantees any negative value has multiple set
// bits.  The predicate's body short-circuits on `x <= 0` to avoid
// the (x & (x-1)) computation underflowing for INT_MIN.
//
// `bound <= 0` is permitted: the predicate returns false for every
// possible x (no positive power of two fits in the empty interval
// [1, bound]).  Equivalent to vacuous-rejection on empty domain.
//
// USAGE PATTERN
// -------------
//
//   // CONTRACT-109 production usage shape: SwissCtrl group width
//   // must be a power of two ≤ 64 (the AVX-512 byte-vector limit).
//   constexpr bool valid_group_width(std::size_t w) noexcept {
//       CRUCIBLE_PRE(crucible::decide::is_power_of_two_le<std::size_t>(w, 64));
//       return true;
//   }
//
//   // At consteval, planted non-power-of-two fails compilation:
//   //   static_assert(valid_group_width(48));  // 48 has bits set in 32+16
//   //                 ↑ rejected: "non-constant condition"
//   // And planted over-bound fails too:
//   //   static_assert(valid_group_width(128));  // 128 > 64
//   //                 ↑ rejected: "non-constant condition"
//
// PRODUCTION CITES (update on adoption per CONTRACT-125)
// ------------------------------------------------------
//   (none yet — first migration batch lands with CONTRACT-109:
//    RecipePool::capacity_, ExprPool::capacity_, SwissCtrl::kGroupWidth,
//    PoolAllocator::ptr_table_size, TraceRing::CAPACITY validation)
//
// ANTI-PATTERNS (review-rejected)
// -------------------------------
//   * `pre (x % 2 == 0)` — accepts 6, 10, 12 (any even number).
//     Misunderstands "power of two" as "divisible by two".
//   * `pre ((x & 1) == 0)` — same as `% 2 == 0`, rejects only odd.
//   * `pre (__builtin_popcount(x) == 1)` — correct power-of-two
//     test, but no bound; misses overflow/oversize cases.
//   * `pre (std::has_single_bit(x))` — same as popcount==1 above;
//     C++20 idiom, still no bound.  Cite this procedure to bind
//     the conjunction.
//   * `pre (x <= K)` alone — bound check without power-of-two
//     enforcement; downstream code that does `x & (x - 1)` to
//     compute a mask silently breaks.
//   * `pre (x > 0 && (x & (x - 1)) == 0 && x <= K)` — correct,
//     but spelled out at every call site; three places to drift,
//     three places to forget the "x > 0" guard against UB on
//     signed T (INT_MIN - 1 is UB).  Always cite this procedure.
template <std::integral T>
[[nodiscard, gnu::const]]
constexpr bool is_power_of_two_le(T x, T bound) noexcept {
    if (x <= T{0}) {
        return false;  // rejects zero and (for signed T) negatives
    }
    if (x > bound) {
        return false;
    }
    // x > 0 implies x - 1 is well-defined for both signed and
    // unsigned T (no underflow / no UB).  Power-of-two test:
    // exactly one bit set ↔ (x & (x - 1)) == 0 for positive x.
    return (x & (x - T{1})) == T{0};
}

// ─── factorization_eq ──────────────────────────────────────────────
//
// Returns true iff `prod(factors) == total` AND the running
// product never overflows T during the multiplication.  Empty
// span returns `total == 1` (vacuous: empty product is the
// multiplicative identity).
//
// The shape: span-quantified MULTIPLICATIVE check.  Distinct from
// the additive shape (no_overflow_sum is binary; this is n-ary)
// AND distinct from the elementwise-quantified shape (all_in_range
// is per-element; this aggregates).  Production usage: 5D
// parallelism config (TP × DP × PP × EP × CP == world_size),
// memory plan total_bytes = sum(slot_sizes) — wait, that's
// additive.  This is the multiplicative branch: any place where
// a product MUST equal a known total.
//
// SEMANTIC NOTE
// -------------
// Overflow during the running product is REJECTED.  Even if the
// wrapped value happens to coincide with `total`, the predicate
// returns false because the mathematical product (in arbitrary-
// precision arithmetic) does NOT equal `total`.  This is the
// canonical "saturating predicate" semantics — Decide procedures
// detect overflow and reject; callers cannot accidentally rely on
// modular arithmetic.
//
// The check uses `__builtin_mul_overflow` per-step, identical to
// the discipline in `no_overflow_mul`.  Total cost: O(n) muls,
// each branch-free (the overflow flag is a CPU register read).
//
// Empty span returns `total == 1`:  ∏_{f ∈ ∅} f = 1 (empty product).
// So `factorization_eq([], 1) == true` and `factorization_eq([], 0)`
// or `factorization_eq([], 5)` is false.  This matches the
// mathematical convention; callers that prefer to reject empty
// span outright should compose with `pre(!factors.empty())`.
//
// Factor of 0 forces the product to 0; equality with `total` then
// requires `total == 0`.  Factor of 1 is the multiplicative
// identity (no effect).  Negative factors on signed T are
// permitted; the running product reflects sign-flip semantics.
//
// USAGE PATTERN
// -------------
//
//   // CONTRACT-110 production usage shape: 5D parallelism factor
//   // decomposition must multiply to world_size.
//   constexpr bool valid_5d_partition(std::span<const uint32_t> dims,
//                                     uint32_t world_size) noexcept {
//       CRUCIBLE_PRE(crucible::decide::factorization_eq(dims, world_size));
//       return true;
//   }
//
//   // At consteval, planted off-by-factor fails compilation:
//   //   constexpr uint32_t bad_dims[] = {8, 2, 4, 1, 2};  // 128
//   //   static_assert(valid_5d_partition(bad_dims, 64));   // 64 != 128
//   //                 ↑ rejected: "non-constant condition"
//
// PRODUCTION CITES (update on adoption per CONTRACT-125)
// ------------------------------------------------------
//   (none yet — first migration batch lands with CONTRACT-110:
//    5D parallelism config validation in Distribution layer +
//    discrete-search partition optimizer in Meridian)
//
// ANTI-PATTERNS (review-rejected)
// -------------------------------
//   * `pre (TP * DP * PP * EP * CP == world_size)` — written
//     out at every call site; misses overflow at the literal
//     multiplication level (the comparison sees a wrapped value
//     and may silently accept).
//   * `pre (std::accumulate(begin, end, 1, multiplies<>) == total)`
//     — same overflow blindness; std::accumulate uses bare `*`.
//   * `pre (factors.size() == 5)` — checks dimensionality but not
//     product; admits a 5-tuple with nonsensical product.
//   * `pre (no_overflow_mul(...) && ...)` chained — verbose and
//     drifts.  Always cite this procedure for the n-ary case.
template <std::integral T>
[[nodiscard, gnu::pure]]
constexpr bool factorization_eq(std::span<const T> factors, T total) noexcept {
    T product{1};
    for (T const& f : factors) {
        if (__builtin_mul_overflow(product, f, &product)) {
            return false;  // overflow — cannot represent the true product
        }
    }
    return product == total;
}

}  // namespace crucible::decide
