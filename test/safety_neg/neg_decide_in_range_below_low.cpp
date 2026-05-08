// SPDX-License-Identifier: Apache-2.0
//
// Negative-compile fixture (HS14 mandate) for crucible::decide::
// in_range, mismatch class #2: VALUE-BELOW-LOW (always-accept /
// drop-low-clause / disjunction violator).
//
// Pins the closed-interval lower-bound clause `lo <= x`.  Witness:
// `in_range(0u, 1u, 10u)`, where 0 is one before the lower endpoint
// of the shifted interval [1, 10].  CRUCIBLE_PRE fires
// `__builtin_trap()` at consteval because the predicate correctly
// returns false; the front-end rejects with "non-constant
// condition".
//
// In production this bug class manifests as: a contract guard
// `pre (decide::in_range(slot_offset, MIN_VALID, MAX_VALID))`
// ought to reject offsets below the floor (uninitialized SlotId
// sentinel UINT32_MAX, stale reused-slot index pointing into
// reserved metadata pages, off-by-one before the start of a CSR
// query range).  A drop-low or always-accept impl admits these,
// leading to:
//
//   - PoolAllocator: slot_ptr where offset < ELEMENT_BASE_OFFSET
//     produces a pointer into the pool's metadata header rather
//     than a tensor data slot — silent metadata corruption on
//     write, silent garbage read.
//   - MemoryPlan: assigning a slot at offset < first_data_offset
//     overlaps the plan's prologue / metadata; subsequent slot
//     dispatches read-modify-write fields they don't own.
//   - Cipher: append at log_offset < HEADER_BYTES overwrites the
//     file header magic / version, breaking deserialization for
//     every reader of the cold tier.
//
// The bug is sneaky because:
//
//   1. Half-predicate impls that drop the LOWER-bound clause look
//      symmetric to the upper-bound case, but a different shape:
//        return x <= hi;            // IGNORED-LO
//      For (0, 1, 10): 0 <= 10 = true.  Buggy impl compiles; this
//      fixture's static_assert PASSES under the buggy impl
//      (predicate returns true → CRUCIBLE_PRE doesn't trap →
//      fixture compiles); CMake regex DOES NOT MATCH → test
//      FAILS, surfacing the regression.  The companion fixture
//      (above-high) does NOT catch IGNORED-LO: for (11, 0, 10),
//      IGNORED-LO returns 11 <= 10 = false, which CRUCIBLE_PRE
//      correctly traps on, so the companion correctly fails to
//      compile (false positive — looks correct).  Only THIS
//      fixture catches IGNORED-LO.
//
//   2. ALWAYS-ACCEPT / DISJUNCTION — both shapes fail this fixture
//      AND the companion (above-high), so the cohort over-pins
//      these bug classes.  Defense in depth.
//
//   3. WRONG-DEFAULT-LOW (`return x <= hi && x >= 0;`) — hardcodes
//      lo=0 instead of using the parameter.  For (0, 1, 10): 0 <=
//      10 && 0 >= 0 = true && true = true.  Buggy impl admits the
//      below-low witness; this fixture catches.  This is a subtle
//      bug because the predicate's API takes lo but the
//      implementation ignores it.  Fixtures that use lo=0 (like
//      the companion) cannot detect this; THIS fixture uses lo=1
//      explicitly so the parameter is load-bearing.
//
// Distinct from the companion fixture (above-high):
//   * below-low (this fixture)   — witness 0 < lo.  Catches
//     ALWAYS-ACCEPT / IGNORED-LO / WRONG-DEFAULT-LOW /
//     DISJUNCTION.  Critically, IGNORED-LO and WRONG-DEFAULT-LOW
//     are caught ONLY by this fixture (companion misses them).
//   * above-high (companion)     — witness 11 > hi.  Catches
//     ALWAYS-ACCEPT (covered) / IGNORED-HI / DISJUNCTION
//     (covered) — and the unique class IGNORED-HI that this
//     fixture does NOT catch.
//
// Anti-pattern targeted: drop-low-clause / always-accept /
// hardcoded-low / wrong-connective.  Specific shapes:
//
//   template <typename T>
//   constexpr bool in_range(T, T, T) { return true; }
//     // ALWAYS-ACCEPT — admits 0 in [1,10].  Caught (also caught
//     // by companion).
//
//   template <typename T>
//   constexpr bool in_range(T x, T, T hi) { return x <= hi; }
//     // IGNORED-LO — admits anything below hi including 0.
//     // CAUGHT BY THIS FIXTURE; missed by companion.
//
//   template <typename T>
//   constexpr bool in_range(T x, T, T hi) { return x >= 0 && x <= hi; }
//     // WRONG-DEFAULT-LOW (hardcodes lo=0) — admits 0 even when
//     // lo=1.  CAUGHT BY THIS FIXTURE; missed by companion (which
//     // uses lo=0).
//
//   template <typename T>
//   constexpr bool in_range(T x, T lo, T hi) {
//       return lo <= x || x <= hi;
//   }
//     // WRONG-CONNECTIVE (disjunction) — admits 0 because 0 <= 10.
//     // Caught (also caught by companion).
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "non-constant condition" / "not a constant expression" /
//   "__builtin_trap" / "call to non-constexpr function".

#include <crucible/safety/Decide.h>
#include <crucible/safety/Pre.h>

#include <cstdint>

namespace {

template <typename T>
[[nodiscard]] constexpr bool gate(T x, T lo, T hi) noexcept {
    CRUCIBLE_PRE(crucible::decide::in_range(x, lo, hi));
    return true;
}

// 0 sits one before the closed-interval lower endpoint of [1, 10].
// Note the deliberate non-zero `lo`: a buggy impl that hardcodes
// `lo=0` (WRONG-DEFAULT-LOW) would falsely accept this witness
// because the predicate's lo parameter is ignored.  in_range
// correctly rejects; CRUCIBLE_PRE's __builtin_trap fires at
// consteval.  Catches always-accept / drop-low / hardcoded-low /
// disjunction bug classes that would silently admit below-floor
// values at every cite (PoolAllocator::slot_ptr below
// ELEMENT_BASE_OFFSET, MemoryPlan slot prologue overlap, Cipher
// header overwrite).
constexpr auto witness =
    gate(std::uint32_t{0}, std::uint32_t{1}, std::uint32_t{10});

}  // namespace

int main() { return 0; }
