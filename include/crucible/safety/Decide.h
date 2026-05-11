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

#include <crucible/effects/EffectRow.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
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
//   * MerkleDag.h:159  — compute_storage_nbytes design-doc cite.
//                        The body uses `__builtin_mul_overflow` to
//                        recover into the Saturated<T> path rather
//                        than assume away overflow via `pre()`,
//                        but the named predicate remains the
//                        authoritative formal description so
//                        `grep decide::no_overflow_mul` finds the
//                        protected arithmetic site (CONTRACT-105).
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
//   * MerkleDag.h:161         — compute_storage_nbytes max_offset /
//                                min_offset / span accumulation chain.
//                                Body uses `__builtin_add_overflow` /
//                                `__builtin_sub_overflow` and recovers
//                                into Saturated<T>; the predicate is
//                                the formal cite for "this addition
//                                does not overflow" (CONTRACT-105).
//   * PoolAllocator.h:138,176 — pool_bytes_ + page_align-1 alignment
//                                round-up + end_offset chain.  Body
//                                uses page_align bookkeeping inside
//                                contracts; the predicate is the
//                                formal cite for the addition site
//                                (CONTRACT-127).
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
//   (no production cite yet.  CONTRACT-109 was originally tagged
//   here as the first migration batch but actually shipped using
//   `decide::is_power_of_two_le` for RecipePool / ExprPool /
//   SwissCtrl capacity invariants — a structurally different
//   predicate.  The pow2-shift overflow predicate awaits a real
//   consumer; plausible future cite sites are bit-shift width
//   guards in Bits.h, ProbeSequence stride amplification, and
//   any `(a << b)` site whose `b` is not statically bounded by
//   `bitwidth(T)`.  If no consumer materializes by 6mo, this
//   predicate is a candidate for CONTRACT-126 trim.)
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
//   (no production cite yet.  CONTRACT-102 was originally tagged
//   here but actually shipped using single-element `decide::in_range`
//   on per-call indexed access — the predicate the code already
//   needed at the boundary, not the whole-span variant.  The
//   `all_in_range` predicate awaits a batch-validation consumer;
//   plausible future cite sites are TensorMeta sizes[] / strides[]
//   non-negative-and-bounded gates (WRAP-TensorMeta-1 #1034) and
//   TraceGraph counter SoA validation (WRAP-TraceGraph-3 #1044).
//   If no consumer materializes by 6mo, this predicate is a
//   candidate for CONTRACT-126 trim.)
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
//   (no production cite yet.  CONTRACT-107 was originally tagged
//   here but actually shipped using `decide::weakly_increasing`
//   (Cipher.h:647 step_id sequence — repeats permitted on idempotent
//   replay).  CONTRACT-114 shipped using the `Monotonic` /
//   `BoundedMonotonic` value-level wrappers (Transaction count_ /
//   Vigil step_) — the type carries the proof at the field level,
//   so a span-wise predicate cite was unnecessary.  The strict
//   variant awaits a real consumer; plausible future cite sites
//   are MerkleDag BranchNode arms in sorted-by-ArmByValue order
//   (#939 WRAP-MerkleDag-3) and any sorted-key span where strict
//   uniqueness must be proven across the entire sequence.  If no
//   consumer materializes by 6mo, this predicate is a candidate
//   for CONTRACT-126 trim.)
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
//   * Arena::epoch_chain monotonicity guard         — Arena.h:310
//     (consecutive epoch-counter span; duplicates admissible across
//     the no-mutation between two ticks of the same value)
//   * Cipher::recovery step-id sequence             — Cipher.h:647
//     (event-log replay step ordering; CONTRACT-114 cite-pair)
//
// Future cites planned for CONTRACT-110 (TraceGraph CSR row-pointer
// offsets) and StorageSlot offset chains.
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
//   * SwissCtrl::kGroupWidth                       — SwissTable.h:79
//     (static_assert; SIMD-group-width 16/32/64 enforcement)
//   * RecipePool ctor — initial_capacity            — RecipePool.h:82
//     (Swiss-table backing store sizing; CONTRACT-109)
//   * ExprPool kDefaultInitialCapacity              — ExprPool.h:329
//     (interner initial-capacity static_assert; CONTRACT-109)
//   * ExprPool kIntCacheSize                        — ExprPool.h:1038
//     (small-int interning cache static_assert; CONTRACT-109)
//   * KernelCache slots-table ctor                  — MerkleDag.h:1256
//     (lock-free open-addressing slot count; CONTRACT-116)
//
// All five sites use the explicit-T form to lock the bound type
// width.  Future cites planned for PoolAllocator::ptr_table_size
// and TraceRing::CAPACITY validation under wider sweeps.
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

// ─── coprime ───────────────────────────────────────────────────────
//
// Returns true iff gcd(|a|, |b|) == 1, i.e. `a` and `b` share no
// common factor other than 1.  Computed via Euclidean algorithm
// over unsigned absolute values; total over inputs (no UB on
// signed extremes including INT_MIN).
//
// SEMANTIC NOTE
// -------------
// Coprimality is a property of the absolute values: coprime(-15, 25)
// returns the same as coprime(15, 25).  Sign is irrelevant.
//
// Edge cases pinned by definition:
//   * coprime(0, 0)   → false  (gcd(0, 0) is conventionally 0)
//   * coprime(0, 1)   → true   (gcd(0, 1) == 1)
//   * coprime(0, n>1) → false  (gcd(0, n) == n)
//   * coprime(1, n)   → true   (1 is the multiplicative identity)
//   * coprime(n, n)   → (n == 1)  (every n>1 shares itself as factor)
//
// The (0, 0) case is the load-bearing edge: gcd(0, 0) is not 1,
// so coprime(0, 0) is false.  Production code that hand-rolls
// "gcd(a, b) == 1" without a zero guard often crashes (division
// by zero in the Euclidean step) or returns garbage; the predicate
// short-circuits and returns a defined false.
//
// USAGE PATTERN
// -------------
//
//   // CONTRACT-109 production usage shape: hash table double-
//   // probing requires the secondary stride to be coprime to
//   // the table capacity (otherwise probe sequence cycles
//   // through a strict subset of slots and lookups can fail
//   // even when free slots exist).
//   constexpr bool valid_secondary_stride(std::size_t stride,
//                                         std::size_t capacity) noexcept {
//       CRUCIBLE_PRE(crucible::decide::coprime<std::size_t>(stride, capacity));
//       return true;
//   }
//
//   // At consteval, planted shared-factor fails compilation:
//   //   static_assert(valid_secondary_stride(15, 25));   // gcd 5
//   //                 ↑ rejected: "non-constant condition"
//
// PRODUCTION CITES (update on adoption per CONTRACT-125)
// ------------------------------------------------------
//   (no production cite yet.  CONTRACT-109 was originally tagged
//   here but actually shipped using `decide::is_power_of_two_le`
//   for RecipePool / ExprPool / SwissCtrl capacity invariants —
//   a separate predicate.  Crucible's SwissTable does not use
//   double-hashing (linear probe over 16-byte SIMD groups), so
//   the originally-anticipated probe-stride coprime guard does
//   not apply.  The coprime predicate awaits a real consumer;
//   plausible future cite sites are Philox stride-key validation
//   for cross-counter independence and any open-addressing hash
//   variant that ever does adopt double-hashing.  If no consumer
//   materializes by 6mo, this predicate is a candidate for
//   CONTRACT-126 trim.)
//
// IMPLEMENTATION NOTE
// -------------------
// The Euclidean algorithm is iterative (no recursion → no stack
// growth at consteval), runs in O(log min(a, b)) modular steps,
// and uses `std::make_unsigned_t<T>` for the running values so
// that signed T inputs (including INT_MIN) convert to a defined
// magnitude without UB.  The unary minus on the unsigned cast
// of a negative signed value is well-defined modular arithmetic
// and yields exactly the unsigned representation of |a|.
//
// ANTI-PATTERNS (review-rejected)
// -------------------------------
//   * `pre (a % b != 0 && b % a != 0)` — checks divisibility ONE
//     way, misses cases like coprime(6, 9): 6 % 9 == 6 (nonzero),
//     9 % 6 == 3 (nonzero), but gcd(6, 9) == 3.
//   * `pre (std::gcd(a, b) == 1)` — correct for non-negative inputs
//     but std::gcd has implementation-defined behavior on signed
//     extremes (libstdc++ contract violation on INT_MIN).
//   * `pre ((a & 1) || (b & 1))` — only checks they aren't both
//     even; misses every other shared prime factor.
//   * `pre (a != b)` — checks inequality but coprime(6, 9) is
//     false despite 6 != 9.
template <std::integral T>
[[nodiscard, gnu::const]]
constexpr bool coprime(T a, T b) noexcept {
    if (a == T{0} && b == T{0}) {
        return false;  // gcd(0, 0) is 0, not 1
    }
    using U = std::make_unsigned_t<T>;
    // Unsigned absolute value: well-defined for all signed T,
    // including INT_MIN (where -INT_MIN as a signed value would
    // be UB but the unsigned cast wraps to the correct magnitude).
    U au = (a < T{0}) ? static_cast<U>(0) - static_cast<U>(a)
                      : static_cast<U>(a);
    U bu = (b < T{0}) ? static_cast<U>(0) - static_cast<U>(b)
                      : static_cast<U>(b);
    while (bu != U{0}) {
        U t = au % bu;
        au = bu;
        bu = t;
    }
    return au == U{1};
}

// ─── Interval<T> + intervals_pairwise_disjoint ─────────────────────
//
// `Interval<T>` is the canonical half-open interval value type for
// the predicate library: `[lo, hi)` — `lo` inclusive, `hi`
// exclusive.  The empty interval `[k, k)` is well-formed (it
// trivially does not intersect anything).  `lo > hi` is malformed
// (no integer x satisfies `lo <= x < hi`); the predicate rejects
// it.
//
// `intervals_pairwise_disjoint` returns true iff:
//   * every interval is well-formed (`lo <= hi`), AND
//   * for every distinct pair (i, j), the intervals do not
//     overlap, i.e., `I_i.hi <= I_j.lo` OR `I_j.hi <= I_i.lo`.
//
// The shape: span-quantified PAIRWISE-RELATIONAL check.  Distinct
// from the elementwise shape (`all_in_range` is per-element),
// distinct from the consecutive-pair shape (`strictly_increasing`
// only relates element i to element i+1), and distinct from the
// n-ary aggregate (`factorization_eq` reduces).  Pairwise
// quantification is its own algorithmic class — O(n²) brute pair
// compare.
//
// SEMANTIC NOTES
// --------------
// 1. Half-open convention.  `[a, b)` matches the dominant C / C++
//    range convention (`std::span`, iterators, byte-offset+size,
//    `[begin, end)`).  A byte slot of `nbytes` at `offset` occupies
//    `[offset, offset + nbytes)` — this exactly matches.
//
// 2. Empty intervals (`lo == hi`) are accepted as well-formed and
//    trivially disjoint from every other interval (the open
//    half-open empty interval contains no integer).  Callers that
//    want to reject empty intervals must compose with an
//    additional predicate at the call site.
//
// 3. Overflow safety.  The predicate compares `lo` and `hi`
//    directly (no addition, no multiplication).  No overflow path
//    exists.  Callers that derive `hi = offset + nbytes` MUST
//    cite `no_overflow_sum(offset, nbytes)` separately to gate the
//    derivation BEFORE building the `Interval<T>` — otherwise the
//    overflow happens at the call site, not inside this predicate.
//
// 4. Algorithm.  Two-pass: pass 1 checks well-formedness in O(n);
//    pass 2 brute-force pair compare in O(n²).  For small n
//    (production cite: dozens to a few hundred memory-plan slots),
//    O(n²) is consteval-cheap and avoids the sort copy that an
//    O(n log n) implementation would need (sorting in place is
//    impossible — the input is `std::span<const Interval<T>>`).
//    Total is branch-light: each inner step is two comparisons +
//    one OR + one `if` → optimizer-friendly.
//
// 5. Total over inputs.  Empty span trivially returns true (zero
//    pairs to check, zero intervals to validate — the empty
//    family of intervals IS pairwise disjoint).  Single-element
//    span returns true iff that one interval is well-formed.
//
// USAGE PATTERN
// -------------
//
//   // CONTRACT-112 production usage shape: MemoryPlan slot
//   // offset assignment must produce non-overlapping byte
//   // intervals among slots that are simultaneously live.
//   constexpr bool valid_memory_plan(
//       std::span<const crucible::decide::Interval<uint64_t>> byte_ivs
//   ) noexcept {
//       CRUCIBLE_PRE(crucible::decide::intervals_pairwise_disjoint(byte_ivs));
//       return true;
//   }
//
//   // At consteval, planted overlap fails compilation:
//   //   constexpr Interval<uint64_t> bad[] = {{0, 64}, {32, 96}};
//   //   static_assert(valid_memory_plan(bad));
//   //                 ↑ rejected: "non-constant condition"
//
// PRODUCTION CITES (update on adoption per CONTRACT-125)
// ------------------------------------------------------
//   * MerkleDag.h:live_intervals_disjoint_at — MemoryPlan live
//     TensorSlots must have pairwise-disjoint byte ranges at every
//     op boundary (CONTRACT-112).
//   * cipher/FederationProtocol.h:cold_blob_regions_pairwise_disjoint
//     — Cipher cold-tier federation blob regions (header + payload)
//     must not overlap before the entry is written or accepted
//     (CONTRACT-119).
//
// ANTI-PATTERNS (review-rejected)
// -------------------------------
//   * `pre (forall i, slots[i].hi <= pool_bytes)` — checks each
//     slot fits in the pool but not pairwise disjointness.  Two
//     slots can both fit and still overlap each other.
//   * `pre (forall i, slots[i].hi <= slots[i+1].lo)` — adjacent-
//     only check.  Misses any overlap with a non-adjacent slot.
//     Common in code that "relies on the slots being sorted" but
//     does not separately verify the sort.
//   * `pre (sum(nbytes) <= pool_bytes)` — capacity check, not
//     overlap check.  A 100-byte pool with two 60-byte slots at
//     offset 0 and 30 fits the capacity (sum = 120 > 100 in this
//     example, but the bug class is broader: a passing capacity
//     check tells you nothing about overlap).
//   * `pre (std::set<uint64_t>(offsets).size() == n_slots)` —
//     uniqueness of LO endpoint only.  Misses `[0, 100)` vs
//     `[50, 150)` (distinct los, overlapping intervals).
//   * `pre (sort && for-adjacent)` written out at every call site
//     — duplicates the sort + scan logic; drifts.  Always cite
//     this procedure for the n-ary pairwise case.
template <std::integral T>
struct Interval {
    T lo{};
    T hi{};
};

template <std::integral T>
[[nodiscard]]
constexpr bool operator==(Interval<T> const& a, Interval<T> const& b) noexcept {
    return a.lo == b.lo && a.hi == b.hi;
}

template <std::integral T, std::size_t N = std::dynamic_extent>
[[nodiscard, gnu::pure]]
constexpr bool intervals_pairwise_disjoint(
    std::span<const Interval<T>, N> ivs
) noexcept {
    // Pass 1: well-formedness (lo <= hi).  An inverted interval
    // is malformed; no integer x satisfies lo <= x < hi.
    for (Interval<T> const& iv : ivs) {
        if (iv.lo > iv.hi) {
            return false;
        }
    }
    // Pass 2: pairwise overlap test.  For distinct (i, j),
    // intervals are disjoint iff one lies entirely to the left of
    // the other in the half-open sense:
    //     I_i ∩ I_j = ∅  ⇔  hi_i <= lo_j  ∨  hi_j <= lo_i.
    for (std::size_t i = 0; i < ivs.size(); ++i) {
        for (std::size_t j = i + 1; j < ivs.size(); ++j) {
            const bool a_left_of_b = ivs[i].hi <= ivs[j].lo;
            const bool b_left_of_a = ivs[j].hi <= ivs[i].lo;
            if (!a_left_of_b && !b_left_of_a) {
                return false;
            }
        }
    }
    return true;
}

// ─── intervals_cover_unit ──────────────────────────────────────────
//
// Returns true iff the input intervals form an EXACT PARTITION of
// the half-open unit range `[0, total)`:
//
//   1. each interval is well-formed (`lo <= hi`),
//   2. each interval is non-empty (`lo < hi`),
//   3. each interval is contained in `[0, total)`,
//   4. all pairs are disjoint (no overlaps),
//   5. the sum of widths equals `total` (no gaps).
//
// (4) AND (5) AND (3) together imply EXACT coverage: pairwise-
// disjoint subintervals each contained in `[0, total)` whose
// widths sum to the unit's width must by pigeonhole cover every
// integer in `[0, total)` exactly once.
//
// Distinct from `intervals_pairwise_disjoint`:
//   * `intervals_pairwise_disjoint` allows GAPS between intervals
//     (just forbids overlap).  Use when slots may have unused
//     bytes between them.
//   * `intervals_cover_unit` (this procedure) FORBIDS GAPS as
//     well as overlaps.  Use when the family must EXACTLY tile
//     a target range — e.g., a partition of compute work, a
//     Cipher cold-tier blob layout that must be hole-free, a
//     5D parallelism shard cover.
//
// SEMANTIC NOTES
// --------------
// 1. Empty span + `total == 0` returns true (vacuous: the empty
//    family partitions the empty range).  Empty span +
//    `total > 0` returns false (cannot cover a nonempty range
//    with no intervals).
//
// 2. Empty intervals (`lo == hi`) are REJECTED.  Mathematically
//    they don't contribute to coverage, but their presence in a
//    "partition" almost always indicates a caller bug (a slot
//    that took zero space, an empty shard).  Surfacing them at
//    construction time prevents downstream confusion.  Callers
//    that genuinely want to allow degenerate empties must filter
//    before calling.
//
// 3. Negative `total` (signed T) returns false.  No partition
//    of an inverted range is well-defined.
//
// 4. Negative `lo` (signed T) on any subinterval returns false
//    (out-of-bounds below).  Vacuously true for unsigned T.
//
// 5. Overflow.  The width sum is bounded above by `total` because
//    each interval is contained in `[0, total)` and they are
//    pairwise disjoint.  But to be safe under adversarial input
//    where the bounds check might be evaded by a clever caller
//    (it can't, but defensive coding), the sum uses
//    `__builtin_add_overflow` and returns false on overflow.
//
// 6. Algorithm.  O(n²) brute-force pairwise compare in pass 3
//    (matching `intervals_pairwise_disjoint`).  For typical
//    production sizes (tens of slots), consteval-cheap.
//
// USAGE PATTERN
// -------------
//
//   // CONTRACT-119 production usage shape: Cipher cold-tier blob
//   // layout — slots must EXACTLY tile the blob's byte range.
//   constexpr bool valid_blob_layout(
//       std::span<const crucible::decide::Interval<uint64_t>> slots,
//       uint64_t blob_bytes
//   ) noexcept {
//       CRUCIBLE_PRE(crucible::decide::intervals_cover_unit(slots, blob_bytes));
//       return true;
//   }
//
//   // At consteval, planted gap fails compilation:
//   //   constexpr Interval<uint64_t> bad[] = {{0, 50}, {60, 100}};
//   //   static_assert(valid_blob_layout(bad, 100));
//   //                 ↑ rejected: gap at [50, 60)
//
// PRODUCTION CITES (update on adoption per CONTRACT-125)
// ------------------------------------------------------
//   (none yet — first migration batch lands with CONTRACT-119:
//    Cipher cold-tier blob layout in Cipher.h, where slots must
//    hole-free tile the on-disk blob; AND CONTRACT-110: 5D
//    parallelism shard cover where TP × DP × PP × EP × CP shards
//    must exactly partition each tensor dim.)
//
// ANTI-PATTERNS (review-rejected)
// -------------------------------
//   * `pre (intervals_pairwise_disjoint(slots) &&
//           all_in_range(slot_los, 0, total))` — disjoint plus
//     contained does NOT imply cover.  Misses the gap class:
//     `{[0, 30), [50, 100)}` is disjoint and contained in
//     `[0, 100)` but does NOT cover.
//   * `pre (sum(widths) == total)` — ignores overlap.  Two
//     overlapping intervals with the right widths sum to total
//     while leaving holes elsewhere.
//   * `pre (sort && for-adjacent: hi[i] == lo[i+1] &&
//           hi[n-1] == total)` — written out at every call site;
//     duplicates the partition logic; drifts.  Always cite this
//     procedure for the n-ary partition case.
//   * `pre (max(hi_i) == total && min(lo_i) == 0)` —
//     boundary-only check, blind to interior gaps and overlaps.
template <std::integral T, std::size_t N = std::dynamic_extent>
[[nodiscard, gnu::pure]]
constexpr bool intervals_cover_unit(
    std::span<const Interval<T>, N> ivs,
    T total
) noexcept {
    // Negative or trivial total: only the empty span partitions
    // an empty range; a negative total has no valid partition.
    if (total < T{0}) {
        return false;
    }
    if (total == T{0}) {
        return ivs.empty();
    }
    // Pass 1: per-interval validation (well-formedness, non-
    // emptiness, containment in [0, total)).
    for (Interval<T> const& iv : ivs) {
        if (iv.lo > iv.hi) {
            return false;  // malformed
        }
        if (iv.lo == iv.hi) {
            return false;  // empty interval — see semantic note 2
        }
        if (iv.lo < T{0}) {
            return false;  // out-of-bounds below (signed T only)
        }
        if (iv.hi > total) {
            return false;  // out-of-bounds above
        }
    }
    // Pass 2: pairwise disjoint (matches intervals_pairwise_disjoint
    // pass 2 semantics).  We don't call the sibling predicate
    // because we'd lose the early-exit benefits of fusing pass 1
    // and pass 3 with this scan.
    for (std::size_t i = 0; i < ivs.size(); ++i) {
        for (std::size_t j = i + 1; j < ivs.size(); ++j) {
            const bool a_left_of_b = ivs[i].hi <= ivs[j].lo;
            const bool b_left_of_a = ivs[j].hi <= ivs[i].lo;
            if (!a_left_of_b && !b_left_of_a) {
                return false;
            }
        }
    }
    // Pass 3: sum of widths.  Each (hi - lo) is non-negative
    // because pass 1 enforced lo < hi.  The running sum is
    // bounded above by `total` thanks to pairwise disjointness +
    // containment (passes 1, 2), but we still use overflow-
    // detecting add for defense-in-depth.  Equality with `total`
    // is the no-gaps witness.
    T width_sum{0};
    for (Interval<T> const& iv : ivs) {
        const T width = static_cast<T>(iv.hi - iv.lo);
        if (__builtin_add_overflow(width_sum, width, &width_sum)) {
            return false;
        }
    }
    return width_sum == total;
}

// ─── tier_replaces ─────────────────────────────────────────────────
//
// Returns true iff `candidate` is at least as strong as `required`
// in Crucible's chain-tier ordering convention — equivalently,
// "a candidate of tier `candidate` may stand in for a slot demanding
// tier `required`."  Lattice-agnostic: works across every chain-tier
// scoped enum following the project convention.
//
// SEMANTIC NOTE
// -------------
// All of Crucible's chain-tier enums (CipherTierTag, DetSafeTier,
// HotPathTier, NumericalTier, ResidencyHeatTier, ProgressClass, …)
// share ONE convention: STRONGER GUARANTEE = HIGHER ordinal.  The
// canonical statement lives in algebra/lattices/CipherTierLattice.h
// docstring lines 60-77 ("Stronger guarantee = HIGHER in the lattice")
// and is repeated verbatim in HotPathLattice.h, DetSafeLattice.h, and
// the sister chain lattices.  Bottom = weakest claim (most permissive
// admission); top = strongest claim (admissible everywhere — safe to
// substitute into any slot).
//
// Under that convention, `replace(candidate, required)` simply asks
// "is the candidate ≥ the requirement in the strength order?"  This
// procedure spells exactly that, lattice-agnostically, via a single
// integer compare on the enum's underlying type.  No SFINAE games,
// no tag dispatch, no per-lattice template specializations: the
// project convention is uniform precisely so a single procedure
// discharges every chain-lattice replacement VC.
//
// CONVENTION GUARD
// ----------------
// A new chain-tier enum that violates "stronger = higher" is a
// project-convention violation, not a `tier_replaces` bug — the
// review-time guard is the docstring discipline at the lattice's
// declaration site (see e.g. CipherTierLattice.h §"Direction
// convention" block).  Adding a new chain enum without that
// docblock is a code-review reject.
//
// CALLED ONCE PER REPLACEMENT GATE
// --------------------------------
// Every tier-pinned production boundary discharges its admission VC
// via this procedure: KernelCache hot↔warm↔cold promotion gates;
// Cipher::publish_hot vs publish_warm vs flush_cold tier transitions;
// Forge Phase E.RecipeSelect's NumericalRecipe admission against the
// kernel's declared recipe tier; BackgroundThread phase promotion
// gates (CONTRACT-117); Augur's drift-attribution dispatch
// distinguishing "Cold-tier S3 latency" from "Hot-path issue".  All
// of these can spell their VC as `tier_replaces(candidate, required)`
// and unify on this procedure.
//
// USAGE PATTERN
// -------------
//
//   constexpr CompiledKernel const& select(
//       CompiledKernel const& candidate,
//       CipherTierTag         required
//   ) noexcept {
//       CRUCIBLE_PRE(crucible::decide::tier_replaces(
//           candidate.storage_tier(), required));
//       return candidate;
//   }
//
//   // Witness, consteval (positive):
//   //   static_assert(crucible::decide::tier_replaces(
//   //       CipherTierTag::Hot, CipherTierTag::Warm));        // ✓
//   //   static_assert(crucible::decide::tier_replaces(
//   //       CipherTierTag::Hot, CipherTierTag::Hot));         // ✓
//   //
//   // Witness, consteval (negative — fixture territory):
//   //   static_assert(!crucible::decide::tier_replaces(
//   //       CipherTierTag::Cold, CipherTierTag::Hot));        // ✓
//
// PRODUCTION CITES (update on adoption per CONTRACT-125)
// ------------------------------------------------------
//   * cipher/CipherTierPromotion.h:can_promote_tier_v /
//     can_demote_tier_v — admission gates for mint_promote /
//     mint_demote / mint_restore + 8 lattice-direction static_asserts
//     (CONTRACT-117 Cipher tier-transition cite, landed first).
//   * Pending: Forge Phase E RecipeSelect admission, KernelCache
//     hot↔warm↔cold gates, Augur drift attribution dispatch.
//   * Closed-with-rationale (no runtime cite): BackgroundThread phase
//     promotion gates.  Originally proposed as a `tier_replaces` cite
//     (Drain → Detect → BuildTrace → MakeRegion).  Post-GAPS-099 the
//     bg-pipeline is `mint_pipeline<...>` over four typed
//     `PermissionedSpscChannel` stages; phase ordering is enforced
//     structurally by the Pipeline typestate (each stage's output
//     channel is the next stage's input channel, checked at compile
//     time by `pipeline_chain<S_i, S_{i+1}>`).  There is no runtime
//     ordinal comparison between phases left to gate, hence no place
//     for a runtime `tier_replaces` cite — the type system already
//     discharges the VC.  See BackgroundThread.h:1087-1102 for the
//     `mint_stage` / `mint_pipeline` chain.
//
// ANTI-PATTERNS (review-rejected)
// -------------------------------
//   * `pre (static_cast<int>(candidate) <= static_cast<int>(required))`
//     — SIGN REVERSED.  Falsely admits a strictly-weaker provider
//     for a stronger requirement.  Catches: Cipher cold-tier blob
//     served to a code path expecting Hot — recovery target silently
//     shifts from μs (RAID redundancy) to minutes (S3 download).
//     Always cite this procedure instead.
//   * `pre (candidate == required)` — IDENTITY-ONLY.  Refuses every
//     legal upgrade.  A Hot-tier candidate (admissible everywhere)
//     cannot satisfy a Warm-tier slot under this rule, defeating the
//     whole point of the chain-lattice convention.
//   * `pre (candidate > required)` — STRICT INEQUALITY.  Refuses
//     identity-replacement (Hot can't replace Hot).  Off-by-one in
//     the opposite direction; equally wrong.
//   * `pre (candidate.satisfies<required>())`-style lattice methods
//     scattered across call sites — does not unify across the
//     sister chain lattices.  Cite this procedure instead so a
//     single review-discoverable VC discharges every chain-lattice
//     replacement.  When grep finds three different spellings of
//     "candidate ≥ required" across the codebase, you've lost the
//     opportunity for cite-deduplication.
//   * `pre (!is_strict_downgrade(c, r))` — defines the negative case
//     and double-negates.  Hard to read at a glance; flips polarity
//     under refactor.  Always state the positive admission.
//
// LATTICE-AGNOSTIC: works across every chain-tier enum following the
// project convention.  The predicate is total (every input pair
// returns a defined bool), pure, and zero-cost — compiles to a
// single `cmp + setge` pair on x86-64 and AArch64 alike.
template <typename TierTag>
    requires std::is_enum_v<TierTag>
[[nodiscard, gnu::const]]
constexpr bool tier_replaces(TierTag candidate, TierTag required) noexcept {
    using U = std::underlying_type_t<TierTag>;
    return static_cast<U>(candidate) >= static_cast<U>(required);
}

// ─── row_subset ────────────────────────────────────────────────────
//
// Returns true iff effect-row `Payload` is a subset of effect-row
// `Ctx` — every Effect atom in Payload is also present in Ctx.  This
// is the substitution principle for capability propagation: a value
// declared `Computation<Payload, T>` may be lifted into a
// `Computation<Ctx, T>` slot iff `row_subset<Payload, Ctx>()`.
//
// SEMANTIC NOTE
// -------------
// Crucible's Met(X) effect-row infrastructure (effects/EffectRow.h)
// already ships `is_subrow_v<R1, R2>` and the matching `Subrow`
// concept; both are consteval-only and serve every CtxFitsStage /
// CtxFitsPipeline / CtxFitsPermissionedProtocol gate via concept-
// constrained templates at the type-system layer.  This procedure
// is the THIN VC-discharge facade — the canonical CRUCIBLE_PRE-
// callable spelling that lives in `decide::*` alongside every
// other admission predicate.
//
// Rationale for the facade (vs naked `is_subrow_v`):
//
//   1. Discoverability — `git grep "decide::"` is the single
//      review-time index into every VC discharge in the codebase.
//      A naked `is_subrow_v<...>` inside CRUCIBLE_PRE doesn't show
//      up in that grep.
//   2. Uniformity — every Decide procedure has the same shape
//      `decide::<predicate>(args)`.  Reviewers learn one shape;
//      apply it everywhere.  When grep finds three different
//      spellings of the same VC across the codebase, you've lost
//      the opportunity for cite-deduplication.
//   3. Documentation home — the production-cite block, anti-pattern
//      catalog, and migration cross-reference live HERE, in one
//      place.
//   4. Future-proofing — when CONTRACT-005's diagnostic
//      enrichment lands richer messages (predicate text + source
//      location), every cite gets the upgrade for free.
//
// ROW SHAPE
// ---------
// Both type parameters must be `effects::Row<Es...>` instantiations
// (any pack of `Effect` atoms).  The procedure delegates to
// `effects::is_subrow_v<Payload, Ctx>`, which compares semantically
// (not structurally) — `Row<Alloc, IO>` and `Row<IO, Alloc>` are
// Subrow-equal under this relation, regardless of the per-call
// canonicalization (METX-2 #474, see EffectRow.h §"Set algebra"
// comment block).
//
// CALLED ONCE PER ROW-ADMISSION GATE
// ----------------------------------
// Every payload-row admission VC discharges via this procedure:
// Stage's CtxFitsStage payload-row admission (Computation's
// declared row vs ExecCtx's row); Pipeline's CtxFitsPipeline
// row-union check (every stage's declared row vs the pipeline-
// level Ctx row); mint_endpoint substrate-row admission;
// Cipher federation's row-intersection at fleet-join (FOUND-K07);
// Forge Phase E.RecipeSelect row constraints (FOUND-J06).  All of
// these can spell their VC as
// `decide::row_subset<PayloadRow, CtxRow>()` and unify on this
// procedure.
//
// USAGE PATTERN
// -------------
//
//   template <typename Payload, typename Ctx>
//   constexpr void process(
//       effects::Computation<Payload, T> const& v
//   ) noexcept {
//       CRUCIBLE_PRE(crucible::decide::row_subset<Payload, Ctx>());
//       // body trusts that every effect Payload may emit is
//       // declared in Ctx; a downstream consumer carrying Ctx
//       // can absorb v's effects without surprise.
//   }
//
//   // Witness, consteval (positive):
//   //   static_assert(crucible::decide::row_subset<
//   //       effects::Row<effects::Effect::Alloc>,
//   //       effects::Row<effects::Effect::Alloc, effects::Effect::IO>
//   //   >());                                                  // ✓
//   //
//   // Witness, consteval (negative — fixture territory):
//   //   static_assert(!crucible::decide::row_subset<
//   //       effects::Row<effects::Effect::IO>,
//   //       effects::Row<effects::Effect::Alloc>
//   //   >());                                                  // ✓
//
// PRODUCTION CITES (update on adoption per CONTRACT-125)
// ------------------------------------------------------
//   (none yet — first migration batches land with CONTRACT-111
//    [Forge Phase L row admission rebrand] and an upcoming
//    Stage / Pipeline cite-pass that converts inline Subrow
//    static_asserts into VC-discharged decide::row_subset cites.)
//
// ANTI-PATTERNS (review-rejected)
// -------------------------------
//   * `pre (effects::row_size_v<Payload> <= effects::row_size_v<Ctx>)`
//     — CARDINALITY-ONLY.  Falsely admits any same-size row, even
//     when the atoms are disjoint (`Row<Block> ⊆ Row<Alloc>` would
//     pass).  The set-membership check is the ONLY correct
//     formulation.
//   * `pre (std::is_same_v<Payload, Ctx>)` — STRUCTURAL EQUALITY.
//     Refuses every legal upgrade (e.g. `Row<Alloc>` admitted into
//     `Row<Alloc, IO>` slot).  Defeats the substitution principle
//     entirely.
//   * `pre (effects::is_subrow_v<Ctx, Payload>)` — DIRECTION
//     REVERSED.  Reads as "Ctx ⊆ Payload" — a Payload row that
//     emits MORE effects than the Ctx admits is the bug we're
//     trying to catch.  Off-by-direction is fatal.
//   * `pre (effects::is_subrow_v<Payload, Ctx>)` direct — works
//     correctly but doesn't appear in `git grep decide::` and
//     duplicates the call-site spelling.  Cite this procedure
//     instead.
//   * Inline `static_assert(Subrow<Payload, Ctx>)` at every
//     production boundary — works for type-system enforcement but
//     can't compose with CRUCIBLE_PRE's debug-mode runtime branch.
//     For the consteval-only path, prefer the concept; for the
//     full quartet (consteval + runtime + [[assume]] + diag),
//     cite this procedure.
//
// CARRIER NOTE: rows have no runtime state — every `row_subset<...>()`
// call resolves at compile time to a single `bool` constant via
// `is_subrow_v`.  The function is `constexpr` (not `consteval`) so
// CRUCIBLE_PRE's debug-mode runtime branch (`if (!(cond))`) accepts
// it; the compiler folds away the call entirely under -O1+.
template <typename Payload, typename Ctx>
[[nodiscard, gnu::const]]
constexpr bool row_subset() noexcept {
    return effects::is_subrow_v<Payload, Ctx>;
}

// ─── fmix_preserves_non_zero ───────────────────────────────────────
//
// Returns true iff `seed != 0 && mix_output != 0`, i.e. iff the
// invocation `mix_output = detail::fmix64(seed)` carried the
// non-zero-preservation invariant that the xxHash / Murmur fmix64
// bijection promises.
//
// ───────────────────────────────────────────────────────────────────
// THE BIJECTION THEOREM
// ───────────────────────────────────────────────────────────────────
//
// `crucible::detail::fmix64` (Expr.h:153, identical to the Murmur3
// finalizer used by xxHash64) is the composition
//
//   k ^= k >> 33;                          // bijective on uint64_t
//                                          // (XOR-shift is its own
//                                          //  inverse over Z/2^64)
//   k *= 0xff51afd7ed558ccdULL;            // bijective on uint64_t
//                                          // (multiplier is odd ⇒
//                                          //  invertible mod 2^64)
//   k ^= k >> 33;                          // bijective
//   k *= 0xc4ceb9fe1a85ec53ULL;            // bijective (also odd)
//   k ^= k >> 33;                          // bijective
//   return k;
//
// Composition of bijections is a bijection.  Direct evaluation
// confirms `fmix64(0) == 0`.  Therefore the inverse image of 0 is
// exactly `{0}`, i.e.
//
//   ∀ seed ∈ uint64_t.  fmix64(seed) == 0  ⟺  seed == 0
//
// Equivalently — the form this predicate pins:
//
//   ∀ seed ∈ uint64_t.  seed ≠ 0  ⟹  fmix64(seed) ≠ 0
//
// ───────────────────────────────────────────────────────────────────
// USAGE
// ───────────────────────────────────────────────────────────────────
//
// At sites that must publish a hash with the `0 = sentinel,
// non-0 = valid` discipline (KernelCache slots, Cipher head_,
// MerkleDag content_hash, RegionNode merkle_hash):
//
//   const uint64_t h = ::crucible::detail::fmix64(seed);
//   CRUCIBLE_PRE(decide::fmix_preserves_non_zero(seed, h));
//   // h is now provably non-zero; safe to construct
//   // Refined<non_zero, ContentHash>{h}.
//
// The predicate pins TWO independent invariants:
//
//   1. `seed != 0` — caller's responsibility (typically: seed mixes
//      a non-zero structural fingerprint with content, or seed is
//      built via `non_zero_seed_xor(constant, ...)`).
//
//   2. `mix_output != 0` — runtime witness that fmix64's bijection
//      property held for this particular seed.  If a future change
//      replaces fmix64 with a non-bijective mixer, this clause
//      catches collisions to zero at the use site rather than
//      silently letting a `0` hash slip through to KernelCache as a
//      "not yet computed" sentinel.
//
// ───────────────────────────────────────────────────────────────────
// PRODUCTION CITES (no production cite yet)
// ───────────────────────────────────────────────────────────────────
//
// CONTRACT-106 was originally tagged here but actually shipped
// using the simpler `decide::is_non_zero` predicate at the
// non-zero-hash FRONTIER (Cipher.h:625, MerkleDag.h:454/531/793/
// 1110/1194 — see is_non_zero PRODUCTION CITES below).  The
// preservation-style predicate `fmix_preserves_non_zero` is a
// stronger structural property — "this xxHash mixing step takes
// a non-zero seed to a non-zero output for every non-zero mix
// constant" — which would gate the BODY of the mixing helper, not
// the output of the chain.  Plausible future cite sites:
//
//   * KernelCache::publish        — content_hash result of fmix
//                                   over (kernel_kind, recipe,
//                                   tile, target_caps).
//   * MerkleDag::compute_merkle   — merkle_hash recursion.
//   * RegionNode ctor             — content_hash field initialization.
//   * Forge Phase H emit          — IR003 fingerprint publication.
//
// Adoption requires the per-mix-constant analysis to be lifted
// into a per-call gate (cheap at consteval, free at runtime).
// If no consumer materializes by 6mo, this predicate is a
// candidate for CONTRACT-126 trim.
//
// Currently spelled per-site as `pre (h != 0)` (3 sites) or as a
// runtime `if (h == 0) std::abort()` (2 sites).  CONTRACT-106
// migration replaces all five spellings with this procedure cite.
//
// ───────────────────────────────────────────────────────────────────
// ANTI-PATTERN CATALOG
// ───────────────────────────────────────────────────────────────────
//
//   pre (seed != 0)
//     // SEED-ONLY — does not witness the bijection at runtime.  If
//     // the hash family is later replaced with a non-bijective
//     // mixer (e.g. CRC-style, or a buggy custom hash that
//     // collapses certain non-zero inputs to 0), zero hashes pass
//     // through silently as "not yet computed" sentinels.  Always
//     // pass BOTH seed and the mix output.
//
//   pre (h != 0)
//     // OUTPUT-ONLY — does not witness that the seed itself was
//     // non-zero.  If the caller forgot to check, fmix64(0) == 0
//     // gets through as a "valid hash."  Always pass BOTH.
//
//   pre (seed != 0 || h != 0)
//     // DISJUNCTION — false only when BOTH are zero.  Allows the
//     // dangerous case `(seed=0, h=0)` to be flagged but admits
//     // `(seed=anything, h=0)` and `(seed=0, h=anything)`, neither
//     // of which is sound.  Use conjunction.
//
// CARRIER NOTE: this predicate does NOT recompute fmix64 internally.
// The caller passes the already-computed mix output, which costs
// zero (the value is in a register from the immediately-preceding
// fmix64 call).  Avoiding the recompute matters: the cite is meant
// for hot paths (KernelCache::publish, Cipher::store).
//
// VC DISCHARGE: this procedure pins the bijection theorem at the
// citation site without proving it.  The theorem itself lives in
// the comment above and in `bench/bench_fmix_bijection.cpp`
// (CONTRACT-090 fuzzer pinning fmix bijection over ~10^9 random
// uint64_t inputs against a slow reference oracle).
[[nodiscard, gnu::const]]
constexpr bool fmix_preserves_non_zero(std::uint64_t seed,
                                       std::uint64_t mix_output) noexcept {
    return seed != 0 && mix_output != 0;
}

// ─── conjunction / disjunction ─────────────────────────────────────
//
// `conjunction(xs)` returns true iff every element of `xs` is true.
// `disjunction(xs)` returns true iff at least one element is true.
//
// Both follow the standard mathematical convention: the empty-domain
// fold is the identity of the operator.
//
//   conjunction({}) ≡ true   (vacuous AND, identity of ∧)
//   disjunction({}) ≡ false  (vacuous OR,  identity of ∨)
//
// ───────────────────────────────────────────────────────────────────
// WHY A FOLD IS NAMED
// ───────────────────────────────────────────────────────────────────
//
// At sites that compose multiple INDEPENDENT decide::* clauses into
// a single CRUCIBLE_PRE precondition, the natural spelling is one
// of:
//
//   pre (decide::no_overflow_mul(a, b)
//     && decide::all_in_range(xs, lo, hi)
//     && decide::strictly_increasing(seq)
//     && seed != 0)
//
// or
//
//   const bool clauses[] = {
//       decide::no_overflow_mul(a, b),
//       decide::all_in_range(xs, lo, hi),
//       decide::strictly_increasing(seq),
//       seed != 0,
//   };
//   pre (decide::conjunction(clauses))
//
// Both are sound; the named-fold spelling adds:
//
//   1. Per-clause introspection — CONTRACT-005's diagnostic machinery
//      can report the INDEX of the failing clause (e.g.
//      "conjunction failed at index 2: strictly_increasing(seq)").
//      The `&&`-chained spelling reports only "the precondition
//      failed."
//   2. Symmetry with `disjunction` — alternative-path admission
//      ("any of these conditions is sufficient") gets the same
//      structural treatment.
//   3. Fuzz target — CONTRACT-090 covers `conjunction` + `disjunction`
//      with one fuzzer each, vs N call-site-specific fuzzers.
//
// ───────────────────────────────────────────────────────────────────
// PRODUCTION CITES (no production cite yet)
// ───────────────────────────────────────────────────────────────────
//
// CONTRACT-113 (IterationDetector::reset multi-field invariant)
// shipped with SIX separate `CRUCIBLE_POST(0, ...)` clauses —
// IterationDetector.h:240-245 — instead of one
// `CRUCIBLE_POST(0, decide::conjunction({...}))`.  This is a
// deliberate diagnostic-locality decision: per-clause posts
// identify exactly WHICH invariant broke, while a single
// conjunction loses the per-clause attribution at violation
// time.  CONTRACT-127 (PoolAllocator::is_initialized) shipped
// using `decide::no_overflow_sum` for the end_offset chain,
// not the conjunction predicate.
//
// Plausible future cite sites for the span-based form:
//
//   * IterationDetector::reset            — multi-field invariant
//     conjunction (signature_len + match_pos + ops_since_boundary
//     all in valid post-reset state).
//   * PoolAllocator::is_initialized       — base + size + offset
//     invariant conjunction.
//   * Vigil mode-transition gate          — alternative-path
//     disjunction (current mode in any of {Recording, Replaying,
//     Aligning}).
//   * Forge Phase H emit_kernel admission — disjunction over
//     vendor-supports flags.
//
// ───────────────────────────────────────────────────────────────────
// ANTI-PATTERN CATALOG
// ───────────────────────────────────────────────────────────────────
//
// `conjunction`:
//
//   pre (xs[0] && xs[1] && xs[2] && ...)
//     // HARD-CODED INDEX FOLD — silently truncates if N grows.
//     // Always cite the procedure with `std::span<const bool>`.
//
//   pre (xs.size() == N)  with all-N-checked elsewhere
//     // PROXY CHECK — passes when all clauses are evaluated, but
//     // doesn't witness their truth values.  Wrong relation.
//
//   pre (std::ranges::any_of(xs, std::identity{}))
//     // OR-INSTEAD-OF-AND — the obvious typo.  any_of is
//     // disjunction; for conjunction use all_of or the named
//     // procedure here.
//
//   bool conjunction(span xs) { return true; }
//     // ALWAYS-TRUE buggy implementation — wraps a no-op.  Caught
//     // by the single-element-false HS14 fixture.
//
// `disjunction`:
//
//   pre (!xs.empty())
//     // EMPTY-CHECK — admits any non-empty span regardless of
//     // contents.  Catches the "empty-domain" bug class but not
//     // "all-false."
//
//   pre (std::ranges::all_of(xs, std::identity{}))
//     // AND-INSTEAD-OF-OR — the inverse typo.  all_of is
//     // conjunction; for disjunction use any_of or the named
//     // procedure.
//
//   bool disjunction(span xs) { return !xs.empty(); }
//     // EMPTY-VACUOUS-TRUE bug — confuses "non-empty" with "some
//     // true."  Caught by the empty-span fixture (correct impl
//     // rejects empty; this impl wrongly accepts non-empty
//     // all-false).
//
//   bool disjunction(span xs) { return true; }
//     // ALWAYS-TRUE buggy implementation.  Caught by the
//     // all-false-multi-element HS14 fixture.
//
// CARRIER NOTE: both procedures short-circuit; the `bool` payload
// is read at most once per element and stops on the first
// disqualifying value.  Cost is O(N) worst case, O(1) common case.
// `gnu::pure` (not `gnu::const`) because the procedure reads
// memory through the span; the optimizer may CSE adjacent calls
// over the same span but cannot hoist past intervening writes to
// it.
//
// VC DISCHARGE: the procedure body uses a hand-rolled loop rather
// than `std::ranges::all_of` / `any_of` because the project's
// no-`<ranges>`-on-hot-path discipline (CLAUDE.md §IV opt-out)
// applies to predicate libraries that may end up cited from hot
// paths via macro expansion under `semantic=enforce`.
[[nodiscard, gnu::pure]]
constexpr bool conjunction(std::span<const bool> xs) noexcept {
    for (bool b : xs) {
        if (!b) return false;
    }
    return true;
}

[[nodiscard, gnu::pure]]
constexpr bool disjunction(std::span<const bool> xs) noexcept {
    for (bool b : xs) {
        if (b) return true;
    }
    return false;
}

// ──────────────────────────────────────────────────────────────────
// implies — material implication `antecedent → consequent`
// (CONTRACT-081)
// ──────────────────────────────────────────────────────────────────
//
// PROCEDURE: returns `!antecedent || consequent`, the standard
// material-implication operator from classical propositional logic.
// Truth table (the only false case is the second row):
//
//     antecedent | consequent | implies
//     -----------+------------+--------
//         T      |     T      |    T
//         T      |     F      |    F   ← only false case
//         F      |     T      |    T
//         F      |     F      |    T
//
// USED BY:
//   - Production migration batches encoding "if X then Y" guarded
//     invariants:
//       * "if region.sealed then region.content_hash != 0"  (Cipher
//         publish gate; CONTRACT-106).
//       * "if recipe.tier == BITEXACT_TC then recipe.tc_shape_hint
//         is set"  (Forge Phase E.RecipeSelect; CONTRACT-111).
//       * "if Cipher.tier == HOT then Cipher.warm_path == nullopt"
//         (Cipher state-machine invariant; CONTRACT-119).
//   - VC discharge for any `pre (X ? Y : true)` ternary-shaped
//     precondition.  The ternary is structurally equivalent to
//     `implies(X, Y)` and the named predicate makes the
//     conditional-invariant obligation grep-discoverable.
//
// VC DISCHARGE: the binary form is the canonical encoding; the
// span-based "all clauses imply each other" form is intentionally
// NOT supplied because:
//
//   1. There is no associative monoid for chained implication
//      (`(A → B) → C` ≠ `A → (B → C)` in general), so a fold has
//      no canonical reduction.
//   2. Real-world chained implication is invariably better
//      expressed as `decide::conjunction({implies(A, B),
//      implies(B, C)})` — explicit transitivity at the call site,
//      which makes the proof obligation legible to readers and
//      enables independent VC discharge of each step.
//
// ANTI-PATTERNS this predicate replaces:
//
//   pre (!region.sealed || region.content_hash != 0)
//     // Reads as "either region is unsealed OR the hash is
//     // non-zero", which buries the *conditional invariant* —
//     // the actual obligation is "sealed regions have non-zero
//     // hashes".  Replace with:
//     pre (decide::implies(region.sealed, region.content_hash != 0))
//
//   pre (region.sealed ? region.content_hash != 0 : true)
//     // Same conditional invariant, ternary-shaped.  The trailing
//     // `: true` is dead syntax.  Replace with the implies cite.
//
//   pre (region.content_hash != 0 || !region.sealed)
//     // De Morgan equivalent of the first form; same legibility
//     // problem.
//
//   bool ok = !ant || cons;
//   contract_assert(ok);
//     // Open-coded; loses the named-predicate cite.  Replace with
//     // implies cite at the assert site.
//
// PROPERTIES (verified by test_decide.cpp):
//   - implies(T, T) == T, implies(T, F) == F.
//   - implies(F, T) == T, implies(F, F) == T (vacuous truth on
//     false antecedent).
//   - !implies(p, q) ⇔ (p && !q)  (negation of implication).
//   - implies(p, q) ⇔ disjunction({!p, q})  (De Morgan / definition).
//   - implies(p, true) == true ∀p  (right-absorbing constant true).
//   - implies(false, q) == true ∀q  (left-absorbing constant false).
//   - Modus ponens: implies(p, q) && p → q.
//
// PRODUCTION CITES (update on adoption per CONTRACT-125)
// ------------------------------------------------------
//   * Expr.h:87           — constructor pre: `positive(nargs_)
//                            → args_ != nullptr`.  The conditional
//                            non-null obligation on the args
//                            pointer when nargs > 0 (CONTRACT-115).
//   * MerkleDag.h:2024,
//     MerkleDag.h:2130    — RegionNode + make_region posts:
//                            `(num_ops > 0 || non-degenerate) →
//                            content_hash != 0`.  Conditional
//                            non-zero-hash invariant for sealed
//                            regions (CONTRACT-106).
//   * PoolAllocator.h:238,
//     PoolAllocator.h:240 — slot_ptr posts: conditional non-null
//                            shape `(slot has been initialized) →
//                            (returned pointer is non-null)`
//                            (CONTRACT-103).
//
// Both arguments are evaluated eagerly under C++ function-call
// semantics — for a guarded null-deref shape `p != nullptr →
// p->field == X`, do NOT use `decide::implies(p != nullptr,
// p->field == X)` because the consequent dereferences null when p
// is null.  Use C++ short-circuit `||`: `p == nullptr || p->field
// == X`.  See feedback_decide_implies_eager_eval.md and the
// Tx::activate UBSan regression at commit 9a0fc58 for the canonical
// cautionary tale.  Pure-operand cases (no null deref in either
// argument) are the safe domain for `decide::implies`.
[[nodiscard, gnu::const]]
constexpr bool implies(bool antecedent, bool consequent) noexcept {
    return !antecedent || consequent;
}

// ──────────────────────────────────────────────────────────────────
// aligned_in_range — composed bounds + alignment predicate
// (CONTRACT-081)
// ──────────────────────────────────────────────────────────────────
//
// PROCEDURE: returns `true` iff `value` lies in the closed
// interval `[low, high]` AND is a multiple of `alignment`.
// `alignment == 0` is rejected (returns `false`) because it does
// not represent any sensible alignment constraint and arithmetic
// `value % 0` is undefined.
//
// Defined as:
//
//     alignment != 0
//       && low <= value
//       && value <= high
//       && (value % alignment) == 0
//
// All four clauses must hold; the predicate is the structural AND
// of bounds (CONTRACT-040 `all_in_range` analog over a single
// element) and alignment (a freestanding modular-arithmetic
// invariant).  The predicate INCLUDES the `alignment != 0` guard
// rather than requiring callers to supply a separate
// `pre (alignment != 0)` because:
//
//   1. Every production cite has the same guard verbatim — DRY.
//   2. A buggy zero-alignment-passes implementation is a
//      classical "it compiled and silently overflows" bug class
//      that a defense-in-depth check catches at the predicate
//      site.
//
// USED BY:
//   - Memory plan offset assignment: `pre (decide::aligned_in_range(
//       offset, 0, pool_bytes - slot_size, slot_alignment))`
//     (CONTRACT-112 MemoryPlan migration).
//   - PoolAllocator slot pointer derivation: `pre (decide::
//     aligned_in_range(slot_offset, 0, capacity_bytes,
//     ELEMENT_ALIGNMENT))` (CONTRACT-103 PoolAllocator migration).
//   - Arena bump-allocator post-condition: `post (r:
//     decide::aligned_in_range(reinterpret_cast<uintptr_t>(r),
//     base, base + capacity, alignment))` (CONTRACT-101 Arena
//     migration).
//
// ANTI-PATTERNS this predicate replaces:
//
//   pre (offset >= 0 && offset <= max_offset && (offset & 0xF) == 0)
//     // Hard-coded alignment as bit-mask.  Loses the alignment
//     // value at the call site (is it 16? 32? read the magic
//     // number).  Replace with explicit alignment parameter:
//     pre (decide::aligned_in_range(offset, 0, max_offset, 16))
//
//   pre (offset % alignment == 0)
//     // ALIGNMENT-ONLY — drops the bounds check.  A buggy or
//     // overflow-prone offset still passes.  Catches at runtime
//     // as out-of-bounds memory access; should catch at predicate
//     // site.
//
//   pre (low <= offset && offset <= high)
//     // BOUNDS-ONLY — drops the alignment check.  A misaligned
//     // pointer in-range still passes.  Catches at runtime as
//     // crash-on-aligned-load (SIGBUS / partial-fault); should
//     // catch at predicate site.
//
//   pre (offset >= low && offset < high && offset % alignment == 0)
//     // OFF-BY-ONE on the upper bound — the predicate uses CLOSED
//     // interval `[low, high]`.  If the call site needs half-open
//     // `[low, high)`, write `aligned_in_range(offset, low,
//     // high - 1, alignment)` explicitly so the off-by-one is
//     // visible.
//
// PROPERTIES (verified by test_decide.cpp):
//   - aligned_in_range(0, 0, 0, 1) == true   (degenerate identity).
//   - aligned_in_range(v, 0, MAX, 1) == (v <= MAX) ∀v  (alignment 1
//     reduces to plain bounds).
//   - aligned_in_range(v, low, high, A) == false ∀v  if low > high
//     (empty interval rejects everything, including aligned values).
//   - aligned_in_range(v, low, high, 0) == false ∀v  (zero
//     alignment is rejected by the guard clause).
//   - For any A > 0: { v : aligned_in_range(v, low, high, A) } is
//     the set { v ∈ [low, high] : A | v }, i.e. multiples of A in
//     the interval.
//
// PRODUCTION CITES (no production cite yet)
// -----------------------------------------
// The "USED BY" block above describes EXPECTED cite sites
// (MemoryPlan offset assignment, PoolAllocator slot pointer
// derivation, Arena bump-allocator post-condition).  The actual
// migrations (CONTRACT-101 / CONTRACT-103 / CONTRACT-112) shipped
// using the simpler component predicates:
//
//   * CONTRACT-112 MemoryPlan migration uses
//     `decide::intervals_pairwise_disjoint` over byte-slot intervals
//     (4 cites today) — alignment is enforced separately by the
//     slot's Refined<aligned_to<N>, T> wrapper at construction, so
//     the in-body predicate doesn't re-bundle alignment + bounds.
//   * CONTRACT-103 PoolAllocator + CONTRACT-101 Arena use
//     `decide::no_overflow_sum` (6 cites total) for offset/end
//     arithmetic, with alignment carried by Refined types on the
//     allocator's `aligned_alignment` parameter.
//
// Reserved for: a hot-path predicate site where (a) alignment is
// dynamically chosen at runtime so it can't be a Refined template
// parameter, (b) the bounds and alignment must be checked in one
// atomic predicate evaluation (e.g. inside a single CRUCIBLE_PRE on
// a function whose body does the unaligned access).  No such site
// exists in the production tree as of CONTRACT-127.  If no consumer
// materializes by the 6-month grace window, this predicate becomes
// a CONTRACT-126 trim candidate; until then it stays as documented
// availability for the dynamic-alignment case.
[[nodiscard, gnu::const]]
constexpr bool aligned_in_range(std::uint64_t value,
                                std::uint64_t low,
                                std::uint64_t high,
                                std::uint64_t alignment) noexcept {
    return alignment != 0u
        && low <= value
        && value <= high
        && (value % alignment) == 0u;
}

// ─── in_range ──────────────────────────────────────────────────────
//
// Returns true iff `lo <= x && x <= hi`, i.e. iff `x` lies in the
// closed interval `[lo, hi]`.  The scalar peer of `all_in_range`
// (span-quantified) and `aligned_in_range` (scalar with alignment).
//
// The catalog's bread-and-butter bounds check.  Every "validated
// index into a sized array" pre-condition in the codebase reduces
// to this shape: SymbolTable indexed access (entry_at(SymbolId)),
// TraceGraph CSR queries (fwd_begin/end, rev_begin/end, op_at),
// PoolAllocator slot dereference, ReplayEngine cursor advance,
// MemoryPlan offset assignment.  Citing the named procedure rather
// than hand-rolling `lo <= x && x <= hi` at every call site:
//
//   * gives reviewers a single grep target ("decide::in_range")
//     for "every bounds check in the codebase"; future hardening
//     (instrumentation, diagnostic enrichment, sanitizer wiring)
//     has ONE touchpoint instead of dozens;
//   * names the obligation in the source.  A reviewer reading
//     `CRUCIBLE_PRE(decide::in_range(i, 0u, num_ops - 1u))` knows
//     immediately what is being asserted; reading `pre (i < num_ops)`
//     requires reconstructing intent (closed vs half-open? signed
//     overflow? zero-element edge case?);
//   * eliminates the boundary-confusion bug class.  The predicate
//     is canonically CLOSED: `[lo, hi]` includes both endpoints.
//     Half-open call sites write `decide::in_range(x, lo, hi - 1)`
//     so the conversion is VISIBLE at the cite.  No more silent
//     `<` vs `<=` typos.
//
// SEMANTIC NOTE
// -------------
// `lo > hi` is permitted: the predicate returns `false` for any
// `x` (empty interval rejects everything) — same convention as
// `all_in_range` over an empty span.  This is intentional per
// CONTRACT-020 design principle #3 (predicates are TOTAL over
// their argument domain).  Callers that want a stricter "valid
// range bounds" check should compose with a separate cite.
//
// USAGE PATTERN
// -------------
//
//   // Production usage shape (CONTRACT-102 SymbolTable migration):
//   const SymbolEntry& entry_at(SymbolId id) const noexcept {
//       CRUCIBLE_PRE(id.is_valid());
//       CRUCIBLE_PRE(crucible::decide::in_range(
//           static_cast<std::size_t>(id.raw()),
//           std::size_t{0},
//           entries_.size() - 1));
//       return entries_[id.raw()];
//   }
//
//   // Production usage shape (CONTRACT-102 TraceGraph migration):
//   const Edge* fwd_begin(OpIndex i) const noexcept {
//       CRUCIBLE_PRE(crucible::decide::in_range(
//           i.raw(), std::uint32_t{0}, num_ops - 1u));
//       return fwd_edges + fwd_offsets[i.raw()];
//   }
//
//   // At consteval, a planted out-of-range value fails compilation:
//   //   constexpr auto witness = gate(11u);  // gate uses in_range(x, 0, 10)
//   //                 ↑ rejected: "non-constant condition" / __builtin_trap.
//
// PRODUCTION CITES (update on adoption per CONTRACT-125)
// ------------------------------------------------------
//   * SymbolTable::entry_at / entry_at_mut    (CONTRACT-102)
//   * TraceGraph::fwd_begin/end/out_degree    (CONTRACT-102)
//   * TraceGraph::rev_begin/end/in_degree     (CONTRACT-102)
//   * TraceGraph::op (node access)            (CONTRACT-102)
//
// Future cites planned across CONTRACT-103 / CONTRACT-108 /
// CONTRACT-112 and the wider `pre (i < cap)` sweep.
//
// ANTI-PATTERNS (review-rejected)
// -------------------------------
//   * `pre (lo < x && x < hi)` — open interval where the catalog
//     is closed.  Off-by-one bug surface: a buggy impl that uses
//     `<` instead of `<=` at one boundary admits values in the
//     wrong half-cell.  Cite `decide::in_range(x, lo, hi)` and
//     adjust by `hi - 1` if half-open is intended.
//   * `pre (lo <= x && x <= hi)` — hand-rolled at every call site.
//     Three places to drift, three places to forget the boundary
//     convention.  Cite the named procedure.
//   * `pre (x < cap)` — CAP-FORM that conflates "cap" with "hi";
//     reviewer can't tell whether the bound is exclusive (cap is
//     a count) or inclusive (cap is a max).  Cite
//     `decide::in_range(x, 0, cap - 1)` to make the conversion
//     visible; the `cap - 1` is half the bug-detection signal.
//   * `pre (decide::all_in_range(std::span(&x, 1), lo, hi))` —
//     SPAN-OF-1 noise.  All_in_range is for ELEMENTWISE checks
//     over a sequence; cite the scalar peer for a single value.
//
// PROPERTIES (verified by test_decide.cpp)
// ----------------------------------------
//   - in_range(x, x, x) == true ∀x         (singleton interval).
//   - in_range(lo, lo, hi) == (lo <= hi)   (lower endpoint).
//   - in_range(hi, lo, hi) == (lo <= hi)   (upper endpoint).
//   - in_range(x, lo, hi) == false  if lo > hi  (empty interval).
//   - For lo <= hi: { x : in_range(x, lo, hi) } = [lo, hi]
//     (predicate IS the closed-interval characteristic function).
template <std::integral T>
[[nodiscard, gnu::const]]
constexpr bool in_range(T x, T lo, T hi) noexcept {
    return lo <= x && x <= hi;
}

// ─── positive ──────────────────────────────────────────────────────
//
// Returns true iff `x > T{0}`, i.e. iff `x` is STRICTLY POSITIVE.
// Zero answers FALSE; for signed T, negative values answer FALSE.
//
// The canonical "non-empty count" / "non-zero size" guard.  Where
// `in_range(x, 1, MAX)` would express the same predicate awkwardly
// (carrying a runtime-unknown upper bound the predicate doesn't
// actually constrain), `positive(x)` says exactly what the
// production site means: "this count must be > 0 to make progress".
//
// ───────────────────────────────────────────────────────────────────
// PREDICATE DEFINITION
// ───────────────────────────────────────────────────────────────────
//
//   ∀ T integral.  ∀ x : T.  positive(x) ⟺ x > 0
//
// For unsigned T this collapses to `x != 0`.  For signed T this
// rejects BOTH zero AND every negative value — `positive(0) = false`,
// `positive(-1) = false`, `positive(1) = true`.  The strict-positive
// semantics are deliberate: callers wanting `x >= 0` (zero-OK
// non-negative) cite a different procedure (or `in_range(x, 0, MAX)`).
//
// ───────────────────────────────────────────────────────────────────
// WHY THIS PROCEDURE EXISTS (per CONTRACT-020 design principles)
// ───────────────────────────────────────────────────────────────────
//
// Five-plus call sites in the codebase write the same `pre (x > 0)`
// shape against arena/pool/region SIZE counters.  Each is a load-
// bearing structural invariant: `Arena::reserve(block_size > 0)`,
// `Arena::alloc_obj(n > 0)`, `Arena::alloc_array(n > 0)`,
// `Arena::raw_alloc(nbytes > 0)`, `Graph::reduction_body(nred > 0)`.
// Without a named cite each is a hand-rolled comparison that drifts
// independently — review can't grep "every count-must-be-positive
// invariant in the codebase" without knowing what spelling each
// author chose (`> 0` vs `>= 1` vs `!= 0` vs `bool(n)`).
//
// The cite gives us:
//
//   * grep target: `decide::positive` finds every such invariant
//     simultaneously.  Future hardening (instrumentation, sanitizer
//     wiring, SMT discharge of "size > 0" obligations) has ONE
//     touchpoint instead of dozens.
//   * intent in the source.  `CRUCIBLE_PRE(decide::positive(n))`
//     reads as "the count is positive"; `pre (n > 0)` reads as a
//     comparison whose semantic interpretation depends on T's
//     signedness convention.
//   * lift path: when the production caller hardens `n` to
//     `Refined<crucible::safety::positive_<size_t>, size_t>`, the
//     predicate name on both sides matches by string equality —
//     trivially-mechanical refactor, no semantic ambiguity.
//
// ───────────────────────────────────────────────────────────────────
// USAGE PATTERN
// ───────────────────────────────────────────────────────────────────
//
//   // Production usage shape (Arena cite — the canonical case):
//   void* alloc_obj(crucible::effects::Alloc, std::size_t n) noexcept
//       pre (::crucible::decide::positive(n))
//   {
//       // body trusts n > 0 via [[assume]] under semantic=ignore;
//       // the bump pointer math below assumes n is a real allocation
//       // request, not a no-op.
//       ...
//   }
//
//   // Production usage shape (Graph::reduction_body — sized-count
//   // structural invariant):
//   GraphNode* reduction_body(std::uint32_t nred, ...)
//       pre (::crucible::decide::positive(nred))
//   {
//       // The reduction has at least one input (nred=0 is meaningless
//       // — there's nothing to reduce).
//       ...
//   }
//
//   // At consteval, a planted nonsense witness fails compilation:
//   //   constexpr auto witness = wrap(0u);  // wrap uses positive(n)
//   //                 ↑ rejected: "non-constant condition" /
//   //                 __builtin_trap.
//
// ───────────────────────────────────────────────────────────────────
// PRODUCTION CITES (update on adoption per CONTRACT-125)
// ───────────────────────────────────────────────────────────────────
//
// First adoption batch lands with this commit:
//   * Arena::reserve_block (block_size)            — Arena.h:39
//   * Arena::alloc_obj (n)                         — Arena.h:183
//   * Arena::alloc_array (n)                       — Arena.h:253
//   * Arena::raw_alloc (nbytes)                    — Arena.h:346
//   * Graph::reduction_body (nred)                 — Graph.h:340
//
// Future cites planned across the wider `pre (x > 0)` sweep —
// any size/count parameter that must be non-empty for the body
// to do meaningful work.
//
// ───────────────────────────────────────────────────────────────────
// ANTI-PATTERNS (review-rejected)
// ───────────────────────────────────────────────────────────────────
//
//   * `pre (x > 0)` — hand-rolled comparison.  Loses the semantic
//     name; reviewers can't grep "all positivity invariants".
//     Cite `decide::positive`.
//   * `pre (x != 0)` — equivalent for unsigned T but DIFFERENT for
//     signed T (admits negatives).  Either deliberately accepts
//     negatives (in which case cite `decide::is_non_zero`) or has
//     a hidden bug (cite `decide::positive` to make the discipline
//     visible).
//   * `pre (decide::in_range(x, 1, std::numeric_limits<T>::max()))` —
//     awkward and carries a meaningless upper bound.  The upper
//     bound is forced by `in_range`'s closed-interval shape but
//     conveys no structural constraint.  Cite the named procedure.
//   * `pre (bool(x))` — uses implicit numeric-to-bool conversion.
//     Unsigned-friendly (0 → false) but coupling the count's truth
//     value to a structural invariant is the bug class
//     `is_non_zero` is for; for "must be positive" the named cite
//     reads cleanly.
//
// ───────────────────────────────────────────────────────────────────
// PROPERTIES (verified by test_decide.cpp)
// ───────────────────────────────────────────────────────────────────
//
//   - positive(T{0}) == false                ∀ integral T.
//   - positive(T{1}) == true                 ∀ integral T.
//   - positive(std::numeric_limits<T>::max()) == true.
//   - For signed T: positive(T{-1}) == false.
//   - For signed T: positive(std::numeric_limits<T>::min()) == false.
//   - { x : positive(x) } = { x : x > 0 }    (predicate IS the
//     strict-positive characteristic function).
template <std::integral T>
[[nodiscard, gnu::const]]
constexpr bool positive(T x) noexcept {
    return x > T{0};
}

// ─── non_negative ──────────────────────────────────────────────────
//
// Returns true iff `T{0} <= x`, i.e. iff `x` is greater-than-or-
// equal-to zero.  Zero answers TRUE; for signed T, every negative
// value answers FALSE.
//
// The canonical "count or capacity" guard where zero is a meaningful
// "nothing to do, but legal" value (a writer that issued zero
// outputs, a buffer of zero requested CPUs, a delta-counter that
// hasn't yet advanced).  Distinct from `positive` (which rejects
// zero) and from `is_non_zero` (which admits negatives for signed T).
//
// ───────────────────────────────────────────────────────────────────
// PREDICATE DEFINITION
// ───────────────────────────────────────────────────────────────────
//
//   ∀ T integral.  ∀ x : T.  non_negative(x) ⟺ T{0} ≤ x
//
// For unsigned T this collapses to `true` (every unsigned value is
// non-negative — the predicate is structurally tautological).  For
// signed T this rejects every value below zero — `non_negative(-1)
// = false`, `non_negative(INT_MIN) = false`, `non_negative(0) =
// true`, `non_negative(1) = true`.  The non-strict (closed-at-zero)
// semantics are deliberate: callers wanting `x > 0` (strict
// positivity, zero rejected) cite `decide::positive` instead.
//
// ───────────────────────────────────────────────────────────────────
// WHY THIS PROCEDURE EXISTS (per CONTRACT-020 design principles)
// ───────────────────────────────────────────────────────────────────
//
// Two distinct cite shapes in the codebase express "x is non-
// negative" without a named procedure:
//
//   * `pre (count >= 0)` — the obvious shape on `int` parameters
//     where zero is admitted (Topology.h::select_warm_cpus).
//   * `pre (!(delta < T{0}))` — the NaN-permissive shape on
//     std::integral T template parameters where zero is admitted
//     (Mutation.h::Monotonic::bump_by).
//
// The two shapes have DIFFERENT bug surfaces.  `count >= 0` is
// straightforwardly inverted-sense-vulnerable; `!(x < T{0})` was
// chosen historically to keep float NaN out of the truth set
// (a side-discipline this procedure does NOT inherit, since
// `std::integral` excludes float entirely).  Both shapes mean the
// same thing semantically; without a named cite they drift
// independently and reviewers cannot grep "every must-be-non-negative
// invariant in the codebase" without knowing each author's chosen
// spelling.
//
// The cite gives us:
//
//   * grep target: `decide::non_negative` finds every such invariant
//     simultaneously.  Contrast: `>= 0` matches mathematical comments
//     and other unrelated comparisons throughout the tree.
//   * intent in the source.  `CRUCIBLE_PRE(decide::non_negative(c))`
//     reads as "the count is non-negative"; `pre (c >= 0)` reads as
//     a comparison whose semantic interpretation depends on T's
//     signedness convention.
//   * structural distinguishment.  At every signed cite, this
//     procedure is the canonical disambiguation between three
//     pointwise-different predicates:
//       - `positive(x)`     ⟺  x > 0       (rejects 0)
//       - `non_negative(x)` ⟺  0 ≤ x       (admits 0, rejects negs)
//       - `is_non_zero(x)`  ⟺  x ≠ 0       (admits negs, rejects 0)
//     The named cite makes the choice visible in the diff window.
//
// ───────────────────────────────────────────────────────────────────
// USAGE PATTERN
// ───────────────────────────────────────────────────────────────────
//
//   // Production usage shape (Topology cite — signed-int param):
//   std::vector<int> select_warm_cpus(int hot_cpu, int count) noexcept
//       pre (::crucible::decide::non_negative(count))
//   {
//       // body trusts count >= 0 via [[assume]] under
//       // semantic=ignore; an empty result is meaningful (no warm
//       // threads requested), but a negative count would underflow
//       // size_t arithmetic downstream.
//       ...
//   }
//
//   // Production usage shape (Mutation cite — std::integral T
//   // template parameter):
//   template <std::integral T, ...>
//   T bump_by(T delta) noexcept
//       pre (::crucible::decide::non_negative(delta))
//   {
//       // The counter advances by delta; delta=0 is meaningful
//       // (no-op atomic load), but a negative delta would step
//       // the counter backwards and violate the wrapper's
//       // monotonicity invariant.
//       ...
//   }
//
//   // At consteval, a planted negative witness fails compilation:
//   //   constexpr auto witness = wrap(int32_t{-1});
//   //                 ↑ rejected: "non-constant condition" /
//   //                 __builtin_trap.
//
// ───────────────────────────────────────────────────────────────────
// PRODUCTION CITES (update on adoption per CONTRACT-125)
// ───────────────────────────────────────────────────────────────────
//
// First adoption batch lands with this commit:
//   * Topology::select_warm_cpus (count)              — Topology.h:350
//   * Monotonic::bump_by (delta)                       — Mutation.h:934
//
// Future cites planned across the wider `>= 0` / `!(x < T{0})` sweep —
// any size/count/delta parameter that admits zero but must reject
// signed negatives.
//
// ───────────────────────────────────────────────────────────────────
// ANTI-PATTERNS (review-rejected)
// ───────────────────────────────────────────────────────────────────
//
//   * `pre (x >= 0)` — hand-rolled comparison.  Loses the semantic
//     name; tautological for unsigned T (the comparison still
//     compiles but the optimizer cannot use it as a discharge).
//     Cite `decide::non_negative`.
//   * `pre (!(x < T{0}))` — NaN-permissive idiom from float-domain
//     code.  Semantically equivalent for std::integral T but
//     reviewers must recognize the NaN-rejection is dead code under
//     the integral constraint.  Cite the named procedure to remove
//     the pretense of NaN handling.
//   * `pre (x == 0 || decide::positive(x))` — disjunctive
//     decomposition of non_negative into "zero or strictly positive".
//     Functionally correct but obscures the structural meaning;
//     reviewers see "either-or" not "non-negative".  Cite the named
//     unified procedure.
//   * `pre (decide::in_range(x, 0, std::numeric_limits<T>::max()))` —
//     awkward and carries a runtime-trivial upper bound.  The
//     upper bound is forced by `in_range`'s closed-interval shape
//     but conveys no structural constraint.  Cite the named
//     procedure.
//   * `pre (static_cast<unsigned>(x) == x)` — wrong-cast confusion.
//     For x in [0, INT_MAX] the round-trip preserves value; for
//     negative x the cast wraps to a huge unsigned value that
//     compares false to the original.  CORRECT result, but the
//     intent is opaque and the implementation is brittle (depends
//     on integer-promotion rules).  Cite `decide::non_negative`.
//
// ───────────────────────────────────────────────────────────────────
// PROPERTIES (verified by test_decide.cpp)
// ───────────────────────────────────────────────────────────────────
//
//   - non_negative(T{0}) == true                 ∀ integral T.
//   - non_negative(T{1}) == true                 ∀ integral T.
//   - non_negative(std::numeric_limits<T>::max()) == true.
//   - For signed T: non_negative(T{-1}) == false.
//   - For signed T: non_negative(std::numeric_limits<T>::min()) == false.
//   - For unsigned T: non_negative(x) == true     ∀ x : T (tautology).
//   - { x : non_negative(x) } = { x : T{0} ≤ x } (predicate IS the
//     non-strict-positive characteristic function).
template <std::integral T>
[[nodiscard, gnu::const]]
constexpr bool non_negative(T x) noexcept {
    return T{0} <= x;
}

// ─── valid_span ────────────────────────────────────────────────────
//
// Returns true iff a `(count, ptr)` pair would form a non-UB
// std::span — that is, the two halves of an unpaired-pointer-and-
// length API agree on their representation of "empty".  Exactly:
// either `count` is zero (the pointer is allowed to be anything,
// including nullptr, because the span is empty) OR the pointer is
// non-null (so reading `count` elements through it is well-defined).
//
// ───────────────────────────────────────────────────────────────────
// PREDICATE DEFINITION
// ───────────────────────────────────────────────────────────────────
//
//   ∀ C integral.  ∀ count : C.  ∀ ptr : const void*.
//     valid_span(count, ptr) ⟺ count = C{0}  ∨  ptr ≠ nullptr.
//
// `count` is integral so the predicate works uniformly across the
// dozen distinct count types Crucible passes through unpaired-
// pointer-and-length APIs (`uint32_t` for TraceRing/MetaLog/MerkleDag
// drains, `std::size_t` for Reflected, `std::uint16_t` for callsite
// scratch, etc.).  `ptr` is `const void*` so any `T*` / `const T*`
// argument decays implicitly without a per-T template instantiation
// at every cite — the predicate is span-shape, not span-element-
// type, so there is no benefit to parameterizing over the element T.
//
// `valid_span` IS the structural shape `std::span<T>(ptr, count)`
// requires for non-UB construction.  The exact invariant std::span
// admits as well-formed:
//
//     span(ptr, n) is non-UB ⟺ n == 0 ∨ ptr ≠ nullptr.
//
// `valid_span(count, ptr)` is the predicate-form of that invariant,
// citable at every CRUCIBLE_PRE / pre site that owns an unpaired
// (count, ptr) parameter pair.
//
// ───────────────────────────────────────────────────────────────────
// USAGE
// ───────────────────────────────────────────────────────────────────
//
//   uint32_t drain(TraceRingEntry* out, uint32_t max_count) noexcept
//       pre (::crucible::decide::valid_span(max_count, out))
//   {
//       if (max_count == 0) return 0;
//       // Body may safely write [out, out + max_count) — the
//       // contract guarantees that whenever max_count > 0, out is
//       // non-null and points to writable storage.  The empty-span
//       // case is handled at the head; the contract permits
//       // (max_count = 0, out = nullptr) without trapping.
//       ...
//   }
//
// The shape captures BOTH the producer and the consumer disciplines
// in one named cite:
//
//   * Producer (caller) discipline: don't pass `(n, nullptr)` when
//     n > 0.  The span has no backing storage; `(n, nullptr)` is a
//     malformed empty-span that masks an upstream bug as a runtime
//     crash on first dereference.
//   * Consumer (callee) discipline: when `count == 0`, do not
//     dereference `ptr`.  The contract permits the caller to pass
//     a sentinel pointer (nullptr or otherwise) for empty spans.
//
// Both halves are cited in one place — `valid_span` IS the named
// invariant.  Migrations from anonymous `pre (n == 0 || p != nullptr)`
// to `pre (decide::valid_span(n, p))` preserve the predicate exactly
// while making every such cite grep-discoverable.
//
// ───────────────────────────────────────────────────────────────────
// PRODUCTION CITES
// ───────────────────────────────────────────────────────────────────
//
//   * TraceRing::drain                   — uint32_t × TraceRingEntry*
//   * TraceRing::drain (3 overloads)     — uint32_t × {SoA arrays}
//   * MetaLog::try_append                — uint32_t × const TensorMeta*
//   * MetaLog::try_contiguous (×2)       — uint32_t × const TensorMeta*
//   * MerkleDag region builders (×3)     — uint32_t × const Op*
//   * MerkleDag LoopNode builder         — uint32_t × const FeedbackEdge*
//   * ReplayEngine::reset                — uint32_t × const TraceEntry*
//   * Reflected::reflect_print           — std::size_t × const T*
//
// Every cite shares the same `count == 0 || ptr != nullptr` shape
// and the same intent (empty-span permission for null pointer).
// Naming the predicate at the catalog deletes 11 distinct anonymous
// disjunctions; future hardening (lifting `(count, ptr)` parameter
// pairs to `Borrowed<T>` / `std::span<const T>` at function-signature
// boundaries — see WRAP-* tasks) propagates through the predicate
// name once.
//
// ───────────────────────────────────────────────────────────────────
// ANTI-PATTERN CATALOG
// ───────────────────────────────────────────────────────────────────
//
//   pre (ptr != nullptr)
//     // OVER-RESTRICTIVE — rejects the legitimate empty-span case.
//     // For (count = 0, ptr = nullptr) the correct predicate admits
//     // (no backing storage needed for zero elements), but this
//     // anti-shape rejects.  Fails at every cite that calls drain()
//     // with max_count = 0 (a common idiom for "size hint of nothing").
//
//   pre (count == 0 && ptr != nullptr)
//     // CONJUNCTION-NOT-DISJUNCTION — admits ONLY (count=0, ptr=non-null),
//     // the impossible-by-design intersection.  Rejects every legitimate
//     // span shape including non-empty data and legitimate empty-with-
//     // sentinel.  Catastrophic over-rejection; the && / || flip is the
//     // canonical "wrong logical-connective" review-reject shape.
//
//   pre (count != 0 || ptr != nullptr)
//     // OR-WITH-WRONG-COUNT-CLAUSE — admits everything except
//     // (count = 0, ptr = nullptr), which is exactly the well-formed
//     // empty-span we should ADMIT.  The bug fires at the legitimate
//     // empty-span case (TraceRing::drain(nullptr, 0) idiom).
//
//   pre (count >= 0 && (count == 0 || ptr != nullptr))
//     // REDUNDANT — count is unsigned in every Crucible cite; the
//     // `count >= 0` clause is a tautology that adds noise without
//     // catching anything.  For signed count types the proper cite
//     // is `decide::non_negative(count) && decide::valid_span(count, ptr)`,
//     // composed via `decide::conjunction`.
//
//   pre (count == 0 || ptr)
//     // BOOL-CONVERSION-IMPLICIT — relies on the implicit pointer-
//     // to-bool conversion.  Functionally equivalent to the correct
//     // form; rejected on style grounds (review-discoverability of
//     // the named cite trumps the 7-character savings).
//
// ───────────────────────────────────────────────────────────────────
// EDGE CASES
// ───────────────────────────────────────────────────────────────────
//
//   - valid_span(0u, nullptr)        == true   (empty span, sentinel ptr)
//   - valid_span(0u, &x)             == true   (empty span, real ptr)
//   - valid_span(5u, &x)             == true   (non-empty, real ptr)
//   - valid_span(5u, nullptr)        == false  (non-empty, null ptr — UB)
//   - valid_span(UINT32_MAX, ptr)    == (ptr != nullptr)   (saturation)
//   - valid_span(0u, garbage_ptr)   == true   (empty span: ptr unread)
//   - valid_span(int8_t{0}, nullptr) == true   (signed zero is structural)
//
// VC DISCHARGE: pinning `valid_span` at the boundary discharges the
// std::span well-formedness obligation.  The body of the protected
// function may then construct std::span(ptr, count) without the
// optimizer adding null-check stubs — the contract guarantees the
// ptr is dereferenceable for every iteration in [0, count).  Under
// `-fcontract-evaluation-semantic=ignore` the predicate folds into
// `[[assume(...)]]`; downstream code is hoisted as if the disjunction
// were proven.
template <std::integral C>
[[nodiscard, gnu::const]]
constexpr bool valid_span(C count, const void* ptr) noexcept {
    return count == C{0} || ptr != nullptr;
}

// ─── is_non_zero ───────────────────────────────────────────────────
//
// Returns true iff `x != T{}`, i.e. iff `x` is not equal to the
// default-constructed (structural-zero) value of its type.  The
// canonical "this struct is initialized" guard.
//
// ───────────────────────────────────────────────────────────────────
// PREDICATE DEFINITION
// ───────────────────────────────────────────────────────────────────
//
//   ∀ T equality-comparable.  ∀ x : T.  is_non_zero(x) ⟺ x ≠ T{}
//
// `T{}` is the structural-zero of T.  For built-in integral T this
// is the literal 0.  For aggregate T (Uuid, ContentHash, MerkleHash,
// etc.) it is the all-fields-default-constructed instance.  T MUST
// be equality-comparable; the predicate uses `operator!=` directly.
//
// The predicate is the natural Decide cite for the recurring idiom
// "this value is not in its post-default-construction state":
//
//   * `cog::content_hash` and `CogMimic::cog_kernel_cache_key`
//     guard against zero-Uuid CogIdentity values (Uuid{0,0} is the
//     reserved sentinel for "uninitialized Cog").
//   * `KernelCache::publish` and `Cipher::store` guard against zero
//     ContentHash sentinels (0 means "slot empty").
//   * `RegionNode` ctor guards against zero merkle_hash, content_hash.
//
// ───────────────────────────────────────────────────────────────────
// USAGE
// ───────────────────────────────────────────────────────────────────
//
//   constexpr std::uint64_t content_hash(CogIdentity const& c) noexcept
//   {
//       CRUCIBLE_PRE(decide::is_non_zero(c.uuid));
//       // c.uuid is now provably non-zero; safe to mix into the
//       // hash without producing the reserved zero-sentinel hash
//       // for an uninitialized Cog.
//       ...
//   }
//
// The template parameter is deduced from the argument; callers
// write `decide::is_non_zero(x)` not `decide::is_non_zero<T>(x)`.
//
// ───────────────────────────────────────────────────────────────────
// PRODUCTION CITES
// ───────────────────────────────────────────────────────────────────
//
//   * cog::content_hash                       — Uuid non-zero guard
//   * CogMimic::cog_kernel_cache_key          — Uuid non-zero guard
//   * Cipher::store                           — Cipher.h:625
//                                               head_ ContentHash advance
//                                               (CONTRACT-106).
//   * KernelCache::publish merkle_hash gate   — MerkleDag.h:454
//                                               (CONTRACT-106).
//   * RegionNode::set_variant_id              — MerkleDag.h:531
//                                               (CONTRACT-106).
//   * make_region recipe->hash gate           — MerkleDag.h:793
//                                               (CONTRACT-106).
//   * KernelCache::lookup snapshot            — MerkleDag.h:1110
//                                               (CONTRACT-106).
//   * KernelCache CAS desired hash            — MerkleDag.h:1194
//                                               (CONTRACT-106).
//
// Where the value-level invariant lives in the type via
// `Refined<non_zero, T>` (the wrapper carries the proof at the type
// level), the cite at the consumer is unnecessary; the cite is for
// the FRONTIER between raw `T` and `Refined<non_zero, T>`, where
// `decide::is_non_zero` is the proof-of-fitness predicate.
//
// ───────────────────────────────────────────────────────────────────
// ANTI-PATTERN CATALOG
// ───────────────────────────────────────────────────────────────────
//
//   pre (x.field_a != 0)
//     // FIELD-MYOPIC — checks one structural slot, ignores the rest.
//     // For Uuid{hi, lo} a `pre (uuid.hi != 0)` admits Uuid{0, k}
//     // for any k≠0, which is non-default-constructed but the test
//     // only covers half the bits.  Always check via `T{}` equality.
//
//   pre (!x.is_zero())
//     // UNDISCIPLINED — works only for types that ship `is_zero()`,
//     // and means the predicate is duplicated at every call site
//     // rather than living in the catalog.  `decide::is_non_zero`
//     // unifies the spelling.
//
//   pre (x != static_cast<T>(0))
//     // INTEGRAL-ONLY — fails to compile for aggregate T.  Use the
//     // generic `T{}` form via this procedure.
//
// ───────────────────────────────────────────────────────────────────
// EDGE CASES
// ───────────────────────────────────────────────────────────────────
//
//   - is_non_zero(int{0}) == false                  (structural zero)
//   - is_non_zero(int{1}) == true
//   - is_non_zero(int{-1}) == true                  (signed: any ≠ 0)
//   - is_non_zero(Uuid{0, 0}) == false              (aggregate zero)
//   - is_non_zero(Uuid{0, 1}) == true               (any field ≠ 0)
//   - is_non_zero(SchemaHash{0}) == false           (strong-id zero)
//
// VC DISCHARGE: this procedure pins the structural-zero invariant
// at the citation site.  It does NOT prove that `T{}` itself is the
// caller's intended sentinel — that is a per-type design decision,
// documented at the type definition site.
template <typename T>
    requires requires (T const& a, T const& b) {
        { a != b } -> std::convertible_to<bool>;
    }
[[nodiscard, gnu::pure]]
constexpr bool is_non_zero(T const& x) noexcept(noexcept(T{} != x)) {
    // Materialize the structural zero into a named local rather than
    // a `T{}` rvalue temporary in the comparison expression.  The
    // body is semantically `x != T{}`, but GCC 16.1.1's consteval
    // evaluator misreports an aggregate `T{}` rvalue as having
    // uninitialized members when the predicate is re-evaluated
    // inside the `[[assume(cond)]]` hint of CRUCIBLE_PRE under
    // static_assert (witnessed at cog::Uuid call sites under
    // CogIdentity self-tests).  Materializing into a `T const zero{}`
    // local forces explicit value-initialization through the user-
    // declared `constexpr T() = default` constructor and side-steps
    // the consteval bug while preserving `noexcept` propagation and
    // the predicate's semantic meaning.
    T const zero{};
    return zero != x;
}

}  // namespace crucible::decide
