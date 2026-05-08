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

}  // namespace crucible::decide
