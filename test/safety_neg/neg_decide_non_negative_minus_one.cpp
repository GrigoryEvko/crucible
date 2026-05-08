// SPDX-License-Identifier: Apache-2.0
//
// Negative-compile fixture (HS14 mandate) for crucible::decide::
// non_negative, mismatch class #1: VALUE-NEGATIVE-SMALL-MAGNITUDE
// (always-accept / inverted-sense / strict-inverse / wrong-cite-to-
// is_non_zero violator).
//
// Pins the non-strict-positive lower-bound clause `T{0} <= x`
// against a SIGNED witness near-zero.  Witness:
// `non_negative(int32_t{-1})`.  CRUCIBLE_PRE fires `__builtin_trap()`
// at consteval because non_negative(-1) correctly returns false; the
// front-end rejects with "non-constant condition".
//
// In production this bug class manifests as: a contract guard
// `pre (decide::non_negative(count))` ought to reject every
// signed-negative value (the canonical "this count is broken
// upstream" sentinel).  An always-accept, inverted-sense, strict-
// inverse, OR wrong-cite-to-is_non_zero impl admits negatives,
// leading to:
//
//   - Topology::select_warm_cpus: a negative `count` underflows
//     `size_t` arithmetic when `same_numa.reserve(count)` or any
//     downstream loop iterates for `[0, count)`.  Result: SIGSEGV
//     or unbounded loop.
//   - Monotonic::bump_by: a negative `delta` steps the counter
//     BACKWARDS via `fetch_add`/`fetch_sub`, violating the wrapper's
//     monotonicity invariant.  Subsequent comparisons against the
//     wrapped counter produce wrong-direction orderings, and the
//     downstream consumer (queue index, SPSC head) reads stale or
//     uninitialized memory.
//
// The bug is sneaky because:
//
//   1. `pre (count >= 0)` and `pre (decide::non_negative(count))`
//      look identical in semantics but a buggy reimplementation
//      that uses `< 0` (strict-inverse) silently admits all
//      negatives.  Code review can spot a typo in the operator
//      direction (`>=` vs `<`) but ONLY if the cite is explicit;
//      a hand-rolled `count >= 0` that some refactor changes to
//      `count < 0` (perhaps under a misapplied De Morgan transform
//      across a `!`) looks innocent in a reviewer's diff window.
//
//   2. ALWAYS-ACCEPT — `return true;` admits everything; this
//      fixture catches.  Defense in depth.
//
//   3. INVERTED-SENSE (`return T{0} >= x;`) — semantic typo where
//      the implementer flipped the comparison.  Admits 0 (correct)
//      AND every signed-negative (incorrect over-admission).  This
//      fixture catches the negative side; the predicate's correct
//      handling of zero is already covered by static_asserts in
//      test_decide.cpp.
//
//   4. STRICT-INVERSE (`return x < T{0};`) — wrong-comparison-
//      direction bug.  Admits negatives only, rejects 0 and
//      positives.  Catches at this fixture.
//
//   5. WRONG-CITE-TO-IS-NON-ZERO (`return x != T{0};`) — admits
//      everything except zero, including negatives.  Catches at
//      this fixture; the zero-witness equivalent CANNOT exist
//      (correct non_negative ADMITS zero).
//
// In Crucible production code, signed cites of `decide::non_negative`
// span both kinds of parameter shapes:
//
//   - Topology::select_warm_cpus(count): plain `int` parameter
//     where the conventional sentinel is "0 means none", "negative
//     means caller error".  A wrong-cite to is_non_zero would
//     produce loop-decrement underflow at the call site:
//     `same_numa.reserve(count)` overflows `size_t` when count is
//     negative, allocator returns SIGSEGV or std::bad_alloc-but-
//     compiled-out-with-fno-exceptions terminate.
//
//   - Monotonic::bump_by(delta): template-typed `T delta` where T
//     is std::integral.  A wrong-cite to is_non_zero would let
//     bump_by step the counter backwards, breaking the
//     monotonicity invariant the wrapper structurally promises.
//     Downstream consumers reading the counter (SPSC tail, queue
//     index, version stamp) see stale-or-future values, leading
//     to torn reads or replay determinism failures.
//
// Distinct from the companion fixture (non_negative_int_min):
//   * minus_one (this fixture)   — witness -1.  Catches ALWAYS-
//     ACCEPT / INVERTED-SENSE (`>= x` instead of `<= x`) /
//     STRICT-INVERSE (`< 0`) / WRONG-CITE-TO-IS-NON-ZERO
//     (`!= 0` admits -1).  Small-magnitude negative.
//   * int_min (companion)        — witness INT32_MIN.  Catches
//     boundary-magnitude bug classes the small-magnitude witness
//     CANNOT detect: abs-based confusion (`std::abs(x) == x`
//     admits INT_MIN due to negation overflow), width-narrowing
//     truncation (`uint8_t(x) <= INT8_MAX` admits INT_MIN due
//     to sign-loss in the cast), and similar boundary-only bugs.
//
// Together the two fixtures span both small-magnitude and boundary-
// magnitude over-admission classes.  This is the minimum HS14 needs.
//
// Anti-pattern targeted: always-accept / inverted-sense / strict-
// inverse / wrong-cite-to-is_non_zero.  Specific shapes:
//
//   template <std::integral T>
//   constexpr bool non_negative(T) { return true; }
//     // ALWAYS-ACCEPT — admits everything including -1.  Caught.
//
//   template <std::integral T>
//   constexpr bool non_negative(T x) { return T{0} >= x; }
//     // INVERTED-SENSE — admits 0 (correct) AND every negative
//     // (incorrect, includes -1).  Caught.
//
//   template <std::integral T>
//   constexpr bool non_negative(T x) { return x < T{0}; }
//     // STRICT-INVERSE — admits negatives only, rejects 0 and
//     // positives.  Caught at -1; production usage would also
//     // start failing at every legitimate count=0 / count=N
//     // call site, so the bug surfaces early.
//
//   template <std::integral T>
//   constexpr bool non_negative(T x) { return x != T{0}; }
//     // WRONG-CITE-TO-IS-NON-ZERO — admits negatives (-1 != 0
//     // = true).  Catches; the COMPANION fixture (int_min) ALSO
//     // catches this same bug, providing defense-in-depth at a
//     // different magnitude.
//
//   template <std::integral T>
//   constexpr bool non_negative(T x) { return x > T{0}; }
//     // STRICT-POSITIVE (collapses to decide::positive) — rejects
//     // 0 incorrectly AND rejects -1 correctly.  This is UNDER-
//     // admission, NOT over-admission; HS14 fixtures only catch
//     // over-admission, so this bug class is caught at production
//     // call sites (Topology::select_warm_cpus(count=0) starts
//     // failing) rather than at this fixture.
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "non-constant condition" / "not a constant expression" /
//   "__builtin_trap" / "call to non-constexpr function".

#include <crucible/safety/Decide.h>
#include <crucible/safety/Pre.h>

#include <cstdint>

namespace {

template <typename T>
[[nodiscard]] constexpr bool gate(T x) noexcept {
    CRUCIBLE_PRE(crucible::decide::non_negative(x));
    return true;
}

// Negative-one is the minimal-magnitude signed witness.
// non_negative(-1) correctly returns false (-1 is below zero);
// CRUCIBLE_PRE's __builtin_trap fires at consteval.  Catches the
// always-accept / inverted-sense / strict-inverse / wrong-cite-to-
// is_non_zero bug classes — the bulk of the over-admission family
// for `non_negative`.  The boundary-magnitude bug class (abs-based
// confusion at INT_MIN, width-narrowing truncation) is caught by
// the companion (int_min) fixture.
constexpr auto witness = gate(std::int32_t{-1});

}  // namespace

int main() { return 0; }
