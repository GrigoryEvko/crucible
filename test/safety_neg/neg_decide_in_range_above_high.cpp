// SPDX-License-Identifier: Apache-2.0
//
// Negative-compile fixture (HS14 mandate) for crucible::decide::
// in_range, mismatch class #1: VALUE-ABOVE-HIGH (always-accept /
// drop-high-clause / disjunction violator).
//
// Pins the closed-interval upper-bound clause `x <= hi`.  Witness:
// `in_range(11u, 0u, 10u)`, where 11 is one past the upper endpoint
// of [0, 10].  CRUCIBLE_PRE fires `__builtin_trap()` at consteval
// because the predicate correctly returns false; the front-end
// rejects with "non-constant condition".
//
// In production this bug class manifests as: a contract guard
// `pre (decide::in_range(i, 0u, num_ops - 1u))` ought to reject
// out-of-range OpIndex values (post-iteration leftover, stale
// SymbolId from a different SymbolTable, off-by-one off the end of
// a TraceGraph CSR query).  A drop-high or always-accept impl
// admits these, leading to:
//
//   - TraceGraph: indexed CSR load with i >= num_ops walks past the
//     end of the offsets array.  fwd_offsets[i+1] reads invalid
//     memory; the resulting Edge* range is garbage; the dependency
//     analysis builds a corrupt dataflow.
//   - SymbolTable: entry_at(SymbolId{N}) where N >= entries_.size()
//     reads past the std::vector — undefined behavior, ASan reports
//     heap-buffer-overflow.
//   - PoolAllocator: slot_ptr(SlotId{N}) where N >= slot_count
//     produces an out-of-pool pointer, which the consumer
//     dereferences as a tensor data slot — silent wrong data.
//
// The bug is sneaky because:
//
//   1. Half-predicate impls that drop the upper-bound clause look
//      similar at a glance:
//        return lo <= x;            // IGNORED-HI
//      A reviewer scanning a 4-line body sees the lower bound is
//      checked and may not notice the upper is missing.  Fixture
//      catches: `in_range(11, 0, 10)` MUST reject.  IGNORED-HI
//      returns true (11 >= 0); CRUCIBLE_PRE doesn't trap; the
//      static_assert compiles; but our witness sets the WRONG
//      expectation under the buggy impl, so the fixture fails to
//      compile.
//
//      Wait — the witness path is: gate(x) calls CRUCIBLE_PRE which
//      requires the predicate to RETURN TRUE at consteval; if it
//      returns FALSE, __builtin_trap fires.  Our witness GATE'S
//      INPUT IS 11; a CORRECT impl returns false → trap → fixture
//      fails to compile = CMake regex matches = test passes.
//
//      A buggy IGNORED-HI impl returns true for (11, 0, 10) → no
//      trap → fixture COMPILES = CMake regex DOES NOT MATCH → test
//      FAILS, surfacing the bug.  This is exactly the HS14 shape:
//      a NEGATIVE-COMPILE fixture is one whose CORRECT-IMPL
//      behavior FAILS TO COMPILE; a BUGGY impl that compiles
//      flags the regression.
//
//   2. ALWAYS-ACCEPT (`return true;`) — admits everything.  Same
//      reasoning as IGNORED-HI: buggy impl compiles; fixture
//      regresses.
//
//   3. DISJUNCTION (`return lo <= x || x <= hi;`) — wrong logical
//      operator.  For (11, 0, 10): 0 <= 11 || 11 <= 10 = true ||
//      false = true.  Buggy impl compiles; fixture regresses.
//
// Distinct from the companion fixture (below-low):
//   * above-high (this fixture)   — witness 11 > hi.  Catches
//     ALWAYS-ACCEPT / IGNORED-HI / DISJUNCTION.
//   * below-low (companion)       — witness 0 < lo.  Catches
//     ALWAYS-ACCEPT (already covered) / IGNORED-LO / DISJUNCTION
//     (already covered) — and the unique class IGNORED-LO that
//     this fixture does NOT catch.
//
// Together the two fixtures pin BOTH bound clauses.  An impl that
// drops one bound check fails one fixture.  An impl that drops
// both, or unconditionally accepts, fails both.
//
// Anti-pattern targeted: drop-clause / always-accept / wrong-
// connective implementations of in_range.  Specific shapes:
//
//   template <typename T>
//   constexpr bool in_range(T, T, T) { return true; }
//     // ALWAYS-ACCEPT — admits 11 in [0,10].  Caught by this
//     // fixture (and by the companion).
//
//   template <typename T>
//   constexpr bool in_range(T x, T lo, T) { return lo <= x; }
//     // IGNORED-HI — admits anything above lo.  Caught by this
//     // fixture; the companion does NOT catch this (companion
//     // tests below-low which IGNORED-HI correctly rejects).
//
//   template <typename T>
//   constexpr bool in_range(T x, T lo, T hi) {
//       return lo <= x || x <= hi;
//   }
//     // WRONG-CONNECTIVE (disjunction instead of conjunction) —
//     // admits 11 because 0 <= 11.  Caught by this fixture.
//
//   template <typename T>
//   constexpr bool in_range(T x, T lo, T hi) {
//       return lo <= x && x < hi;
//   }
//     // OPEN-INTERVAL HIGH (`<` instead of `<=` at hi) — rejects
//     // 11 (correct) AND rejects 10 (incorrect; closed interval
//     // includes hi).  This fixture does NOT catch this bug; the
//     // positive test_decide.cpp witnesses for `in_range(10, 0,
//     // 10) == true` catch it.
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

// 11 sits one past the closed-interval upper endpoint of [0, 10].
// in_range correctly rejects; CRUCIBLE_PRE's __builtin_trap fires
// at consteval.  Catches always-accept / drop-high / disjunction
// bug classes that would silently admit out-of-range indices at
// every cite (SymbolTable::entry_at, TraceGraph::fwd_begin/end,
// PoolAllocator::slot_ptr, ReplayEngine cursor advance).
constexpr auto witness =
    gate(std::uint32_t{11}, std::uint32_t{0}, std::uint32_t{10});

}  // namespace

int main() { return 0; }
