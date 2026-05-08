// SPDX-License-Identifier: Apache-2.0
//
// Negative-compile fixture (HS14 mandate) for crucible::decide::
// positive, mismatch class #1: VALUE-EQUAL-ZERO (always-accept /
// off-by-one-inclusive / inverted-sense violator).
//
// Pins the strict-positive lower-bound clause `T{0} < x`.  Witness:
// `positive(0u)` for `T = uint64_t`; the canonical "no progress
// possible" sentinel.  CRUCIBLE_PRE fires `__builtin_trap()` at
// consteval because positive(0) correctly returns false; the
// front-end rejects with "non-constant condition".
//
// In production this bug class manifests as: a contract guard
// `pre (decide::positive(count))` ought to reject zero (the
// canonical "no work to do" / "uninitialized" sentinel).  An
// always-accept or off-by-one-inclusive impl admits zero, leading
// to:
//
//   - Arena::reserve_block: zero-size block bump-pointer math
//     produces a degenerate block that subsequent allocations
//     then bump-overflow into the arena's metadata header.
//   - Arena::alloc_obj / alloc_array / raw_alloc: zero-size
//     allocation returns a pointer that aliases the previous
//     allocation's tail; first non-zero write past offset 0
//     corrupts the previous slot.
//   - Graph::reduction_body: nred=0 reduction with no inputs;
//     downstream MemoryPlan + Mimic emit code reads input_slots[]
//     before the first valid slot, dereferencing a zero-size
//     allocation or stale uninitialized memory.
//
// The bug is sneaky because:
//
//   1. `pre (count > 0)` and `pre (decide::positive(count))` look
//      identical in semantics but a buggy reimplementation that
//      uses `>= 0` (off-by-one inclusive) silently admits zero.
//      Code review can spot a typo in the operator (`>` vs `>=`)
//      but ONLY if the cite is explicit; a hand-rolled `count > 0`
//      that some refactor changes to `count >= 0` looks innocent
//      in a reviewer's diff window.
//
//   2. ALWAYS-ACCEPT / WRONG-CONNECTIVE — both shapes fail this
//      fixture, so the cohort over-pins these bug classes.
//      Defense in depth.
//
//   3. INVERTED-SENSE (`return x <= T{0};`) — semantic typo where
//      the implementer flipped the comparison.  positive(0) =
//      0 <= 0 = true (incorrectly admits).  This fixture catches.
//
// Distinct from the companion fixture (positive_negative):
//   * zero (this fixture)        — witness 0.  Catches ALWAYS-
//     ACCEPT / OFF-BY-ONE-INCLUSIVE (`>= 0`) / INVERTED-SENSE
//     (`<= 0`).  All three admit zero.
//   * negative (companion)       — witness -1 for signed T.
//     Catches the canonical "is_non_zero confusion" bug class
//     where a refactor accidentally cited the wrong predicate
//     (is_non_zero accepts negatives; positive must reject them).
//
// Anti-pattern targeted: always-accept / off-by-one / inverted /
// wrong-cite.  Specific shapes:
//
//   template <std::integral T>
//   constexpr bool positive(T) { return true; }
//     // ALWAYS-ACCEPT — admits everything including 0.  Caught.
//
//   template <std::integral T>
//   constexpr bool positive(T x) { return x >= T{0}; }
//     // OFF-BY-ONE-INCLUSIVE — admits 0.  Caught.
//
//   template <std::integral T>
//   constexpr bool positive(T x) { return x <= T{0}; }
//     // INVERTED-SENSE — admits 0 (and every negative for
//     // signed T).  Caught.
//
//   template <std::integral T>
//   constexpr bool positive(T x) { return x != T{0}; }
//     // WRONG-CITE (collapses to is_non_zero) — admits 0?  No,
//     // 0 != 0 = false → rejects 0; this fixture passes (looks
//     // correct).  But the COMPANION fixture (negative) catches
//     // this: -1 != 0 = true → admits negative.  Hence the two-
//     // fixture discipline.
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
    CRUCIBLE_PRE(crucible::decide::positive(x));
    return true;
}

// Zero is the canonical "no progress possible" sentinel.  positive
// correctly rejects it; CRUCIBLE_PRE's __builtin_trap fires at
// consteval.  Catches always-accept / off-by-one-inclusive (`>= 0`) /
// inverted-sense (`<= 0`) bug classes that would silently admit the
// no-work sentinel at every Arena cite (block_size=0, n=0, nbytes=0)
// and Graph::reduction_body (nred=0).  The wrong-cite to
// is_non_zero is caught by the companion (negative) fixture.
constexpr auto witness = gate(std::uint64_t{0});

}  // namespace

int main() { return 0; }
