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

}  // namespace crucible::decide
