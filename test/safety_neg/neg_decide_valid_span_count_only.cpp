// SPDX-License-Identifier: Apache-2.0
//
// Negative-compile fixture (HS14 mandate) for crucible::decide::
// valid_span, mismatch class #1: SMALL-MAGNITUDE (always-accept /
// inverted-sense / dropped-zero-clause / off-by-one violator).
//
// Pins the disjunction `count == C{0} || ptr != nullptr` against a
// SMALL-MAGNITUDE non-empty-span witness paired with a NULL pointer.
// Witness: `valid_span(uint32_t{1}, nullptr)`.  CRUCIBLE_PRE fires
// `__builtin_trap()` at consteval because valid_span(1u, nullptr)
// correctly returns false (a non-empty span CANNOT be null-backed);
// the front-end rejects with "non-constant condition".
//
// In production this bug class manifests as: a contract guard
// `pre (decide::valid_span(count, ptr))` ought to reject every
// `(count > 0, ptr = nullptr)` tuple — the canonical "the span has
// no backing storage but claims non-zero elements" sentinel.  An
// always-accept, inverted-sense, dropped-zero-clause, OR off-by-one
// impl admits this UB tuple, leading to:
//
//   - TraceRing::drain(nullptr, max_count > 0): the body trusts the
//     contract and writes [out, out + max_count); dereferencing a
//     null `out` SIGSEGVs on first store.
//   - MetaLog::try_append(metas = nullptr, n > 0): same shape — the
//     body memcpy's `n` TensorMetas through `metas`; null-deref on
//     the first byte.
//   - MerkleDag region builders (build_region_node / make_region /
//     make_loop_node): a `(num_ops > 0, ops = nullptr)` tuple causes
//     the iteration loop to crash on first slot read, but only AFTER
//     incomplete RegionNode construction has corrupted the arena
//     bump cursor.  The corruption persists past the SIGSEGV; the
//     same arena's next allocation lands inside the half-built node.
//   - ReplayEngine::reset(region) where region->ops = nullptr but
//     region->num_ops > 0: the engine stores the dangling cursor
//     and crashes on the FIRST replay step, not at reset() — masks
//     the upstream bug as a downstream crash.
//
// The bug is sneaky because:
//
//   1. ALWAYS-ACCEPT — `return true;` admits everything; this
//      fixture catches at the smallest-magnitude witness.  Defense
//      in depth.
//
//   2. INVERTED-SENSE (`return count != 0 && ptr == nullptr;`) —
//      the OR-AND flip.  Admits exactly the UB tuples this predicate
//      EXISTS to reject.  Catastrophic; this fixture catches at
//      `valid_span(1, nullptr)` where the buggy predicate returns
//      true (`1 != 0 && nullptr == nullptr` → true) and the correct
//      predicate returns false.
//
//   3. DROPPED-ZERO-CLAUSE (`return ptr != nullptr;`) — sloppy
//      strict-non-null guard, omitting the empty-span case.  At
//      this fixture's witness `(1, nullptr)`: buggy returns false
//      → REJECTS → SAME ANSWER as correct → NOT CAUGHT here.
//      Caught instead at the OPPOSITE polarity: `valid_span(0,
//      nullptr)` should admit, dropped-zero-clause rejects, surfaces
//      at the consumer call site (TraceRing::drain(nullptr, 0)
//      idiom).  Documented for completeness; not the primary catch
//      at this fixture.
//
//   4. OFF-BY-ONE-IN-COUNT-CLAUSE (`return count <= 1 || ptr !=
//      nullptr;`) — a fence-post error in the zero-comparison.
//      At witness `(1, nullptr)`: buggy returns `1 <= 1 || false`
//      = true, admits.  Correct returns `1 == 0 || false` = false,
//      rejects.  Caught.
//
//   5. OR-WITH-WRONG-COUNT-CLAUSE (`return count != 0 || ptr !=
//      nullptr;`) — inverts the count side of the disjunction.
//      At witness `(1, nullptr)`: buggy returns `1 != 0 || false`
//      = true, admits.  Correct returns false.  Caught.  This is
//      perhaps the most insidious typo because it READS as if it
//      handles "non-empty count" correctly when it really admits
//      every non-zero count regardless of pointer validity.
//
//   6. WIDTH-NARROWING-COUNT (`return uint8_t(count) == 0 || ptr
//      != nullptr;`) — at witness `(1, nullptr)`: buggy `uint8_t(1)
//      == 0` is false, disjunction continues to `nullptr != nullptr`
//      = false → returns false → SAME ANSWER as correct → NOT
//      CAUGHT here.  Caught instead by the COMPANION fixture at
//      `valid_span(256, nullptr)` where uint8_t(256) = 0 admits.
//
// In Crucible production code, `(count, ptr)` parameter pairs
// straddle every unpaired-pointer-and-length API: the TraceRing
// drain family, MetaLog append/contiguous, MerkleDag region
// builders, ReplayEngine reset, the Reflected reflection harness.
// Eleven distinct cite sites currently use the anonymous shape
// `count == 0 || ptr != nullptr` — naming the predicate at the
// catalog deletes 11 disjunctions from review noise and makes
// every cite grep-discoverable.
//
// Distinct from the companion fixture (valid_span_truncation):
//   * count_only (this fixture)        — witness (1, nullptr).
//     Catches ALWAYS-ACCEPT / INVERTED-SENSE / OFF-BY-ONE-IN-COUNT-
//     CLAUSE / OR-WITH-WRONG-COUNT-CLAUSE.  Small-magnitude.
//   * truncation (companion)            — witness (256, nullptr).
//     Catches WIDTH-NARROWING bug classes the small-magnitude
//     witness cannot detect: low-byte-zero truncation (uint8_t(256)
//     = 0 admits), int8_t-cast count narrowing, etc.
//
// Together the two fixtures span both small-magnitude and width-
// narrowing over-admission classes for `valid_span`.  This is the
// minimum HS14 needs.
//
// Anti-pattern targeted: always-accept / inverted-sense / off-by-
// one / or-with-wrong-count.  Specific shapes:
//
//   template <std::integral C>
//   constexpr bool valid_span(C, const void*) noexcept {
//       return true;
//   }
//     // ALWAYS-ACCEPT — admits every (count, ptr) including
//     // (1, nullptr).  Caught at this fixture.
//
//   template <std::integral C>
//   constexpr bool valid_span(C count, const void* ptr) noexcept {
//       return count != C{0} && ptr == nullptr;
//   }
//     // INVERTED-SENSE — admits exactly the UB tuples (count > 0,
//     // ptr = nullptr) which the predicate EXISTS to reject.
//     // Catastrophic.  Caught at (1, nullptr).
//
//   template <std::integral C>
//   constexpr bool valid_span(C count, const void* ptr) noexcept {
//       return count <= C{1} || ptr != nullptr;
//   }
//     // OFF-BY-ONE — fence-post on the empty-span clause.  Admits
//     // (1, nullptr) where correct rejects.  Caught at (1, nullptr).
//
//   template <std::integral C>
//   constexpr bool valid_span(C count, const void* ptr) noexcept {
//       return count != C{0} || ptr != nullptr;
//   }
//     // OR-WITH-WRONG-COUNT-CLAUSE — flip on the count side of the
//     // disjunction.  Admits everything except (0, nullptr), which
//     // is exactly the legitimate empty-span case the predicate
//     // SHOULD admit.  Reads correct in code review; bug fires at
//     // both (1, nullptr) (over-admit) and (0, nullptr) (over-
//     // reject) consumer call sites.  Caught at (1, nullptr).
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "non-constant condition" / "not a constant expression" /
//   "__builtin_trap" / "call to non-constexpr function".

#include <crucible/safety/Decide.h>
#include <crucible/safety/Pre.h>

#include <cstdint>

namespace {

template <typename C>
[[nodiscard]] constexpr bool gate(C count, const void* ptr) noexcept {
    CRUCIBLE_PRE(crucible::decide::valid_span(count, ptr));
    return true;
}

// (count = 1, ptr = nullptr) is the minimal-magnitude UB witness
// for `valid_span` — a non-empty span backed by null storage.  The
// correct predicate returns false (count != 0 AND ptr == nullptr,
// disjunction false); CRUCIBLE_PRE's __builtin_trap fires at
// consteval.  Catches the always-accept / inverted-sense / off-by-
// one / or-with-wrong-count bug classes — the bulk of the over-
// admission family for `valid_span`.  The width-narrowing bug class
// (uint8_t-truncation at low-byte-zero magnitudes) is caught by the
// companion (truncation) fixture.
constexpr auto witness = gate(std::uint32_t{1}, static_cast<const void*>(nullptr));

}  // namespace

int main() { return 0; }
