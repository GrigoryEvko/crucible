// SPDX-License-Identifier: Apache-2.0
//
// Negative-compile fixture (HS14 mandate) for crucible::decide::
// valid_span, mismatch class #2: WIDTH-NARROWING-TRUNCATION (low-
// byte-zero magnitude / signed-narrowing-cast / count-type-narrowed
// violator).
//
// Pins the disjunction `count == C{0} || ptr != nullptr` against a
// LOW-BYTE-ZERO-MAGNITUDE non-empty-span witness paired with a NULL
// pointer.  Witness: `valid_span(uint32_t{256}, nullptr)`.
// CRUCIBLE_PRE fires `__builtin_trap()` at consteval because
// valid_span(256u, nullptr) correctly returns false (the count is
// non-zero AND the pointer is null); the front-end rejects with
// "non-constant condition".
//
// This fixture exists to catch width-narrowing-truncation bug
// classes that the companion (count_only) fixture CANNOT detect.
// The bug shapes:
//
//   template <std::integral C>
//   constexpr bool valid_span(C count, const void* ptr) noexcept {
//       return static_cast<std::uint8_t>(count) == 0 || ptr != nullptr;
//   }
//     // WIDTH-NARROWING-TRUNCATION at the count clause.
//     // For C=uint32_t, count=1: uint8_t(1) = 1, 1 == 0 is FALSE,
//     //   disjunction continues to ptr != nullptr.  For witness
//     //   (1, nullptr) buggy returns FALSE → SAME ANSWER as
//     //   correct → NOT CAUGHT by count_only fixture.
//     // For C=uint32_t, count=256: uint8_t(256) = 0 (low byte zero,
//     //   high byte truncated), 0 == 0 is TRUE → disjunction
//     //   short-circuits to TRUE → admits (256, nullptr).
//     //   Correct rejects.  THIS FIXTURE CATCHES.
//
//   template <std::integral C>
//   constexpr bool valid_span(C count, const void* ptr) noexcept {
//       return static_cast<int>(count) <= 0 || ptr != nullptr;
//   }
//     // SIGNED-NARROWING-CAST at the count clause.
//     // For C=uint32_t, count=256: int(256) = 256, 256 <= 0 is
//     //   FALSE, disjunction continues to ptr != nullptr.  For
//     //   witness (256, nullptr) buggy returns FALSE → SAME ANSWER
//     //   as correct → NOT CAUGHT here.  Hmm — different bug needs
//     //   different witness.
//     // For C=uint32_t, count=UINT32_MAX (interpreted as -1):
//     //   int(UINT32_MAX) = -1 (impl-defined cast), -1 <= 0 is
//     //   TRUE → admits (UINT32_MAX, nullptr).  Correct rejects.
//     //   This particular bug needs a UINT32_MAX witness rather
//     //   than a 256 witness.  Listed for completeness; the
//     //   primary catch at this fixture is the low-byte-zero
//     //   uint8_t-truncation shape above.
//
//   template <std::integral C>
//   constexpr bool valid_span(C count, const void* ptr) noexcept {
//       return (count & 0xFFu) == 0 || ptr != nullptr;
//   }
//     // BIT-MASK-INSTEAD-OF-EQUALITY (variant of width-narrowing).
//     // For C=uint32_t, count=256 (= 0x100): 0x100 & 0xFF = 0x00,
//     //   0 == 0 is TRUE → admits (256, nullptr).  Correct
//     //   rejects.  Same magnitude class as uint8_t-truncation;
//     //   THIS FIXTURE CATCHES.
//
// In Crucible production code, the count parameter type varies
// across cite sites (uint32_t for TraceRing/MetaLog/MerkleDag,
// std::size_t for Reflected, std::uint16_t for SoA scratch arrays).
// A buggy reimplementation that narrows count to uint8_t or applies
// a `& 0xFF` mask would silently admit tuples like:
//
//   - TraceRing::drain(nullptr, 256): a buggy uint8_t-truncated
//     impl admits, the body trusts the contract and writes 256
//     entries through nullptr.  SIGSEGV on first store.  In
//     practice the test suite exercises max_count = 1024 (large-
//     drain stress) and max_count = 8 (small-drain partial); 256
//     falls in the middle and would be the first failing magnitude
//     under a low-byte-zero truncation bug.
//   - MerkleDag region builders with num_ops = 256: a 256-op region
//     is well within typical model size (a 7B-parameter LLM
//     records ~6000 ops/iteration), so the (256, nullptr) tuple
//     is a realistic upstream-bug shape.  Buggy impl corrupts the
//     RegionNode under construction; the corruption persists past
//     the SIGSEGV and the next region-builder call lands inside
//     the half-built node.
//
// The bug is sneaky in code review because:
//
//   1. The narrowing cast READS as innocent micro-optimization
//      ("the count fits in a byte for our typical batch sizes").
//      But the predicate's job is correctness across the full
//      domain of C, not just the typical bucket.
//
//   2. Unit tests on small-magnitude counts (1, 5, 42) PASS —
//      uint8_t(N) for N < 256 equals N; the truncation is a no-op.
//      The bug only manifests at low-byte-zero magnitudes (256,
//      512, 1024, 65536), which thin test coverage typically
//      omits.
//
//   3. The bug class is structural, not a typo: it's the
//      semantically-wrong count predicate, not a misspelling.
//      Linters and clang-tidy cannot flag it.  Even property-based
//      fuzzers would need to specifically sample low-byte-zero
//      magnitudes to catch.
//
// Distinct from the companion fixture (valid_span_count_only):
//   * count_only (companion)             — witness (1, nullptr).
//     Catches ALWAYS-ACCEPT / INVERTED-SENSE / OFF-BY-ONE / OR-
//     WITH-WRONG-COUNT-CLAUSE.  Small-magnitude.  CANNOT detect
//     width-narrowing because uint8_t(1) = 1 ≠ 0.
//   * truncation (this fixture)          — witness (256, nullptr).
//     Catches WIDTH-NARROWING-TRUNCATION (uint8_t(256) = 0 admits),
//     BIT-MASK-INSTEAD-OF-EQUALITY (count & 0xFF == 0).  The
//     ALWAYS-ACCEPT and INVERTED-SENSE bugs are ALSO caught here
//     (defense in depth), but the unique catch is at the low-
//     byte-zero magnitude.
//
// Together the two fixtures span both small-magnitude and width-
// narrowing over-admission classes.  This is the minimum HS14 needs.
//
// Anti-pattern targeted: width-narrowing-truncation / bit-mask-
// instead-of-equality at the low-byte-zero boundary.  Specific
// shapes:
//
//   template <std::integral C>
//   constexpr bool valid_span(C count, const void* ptr) noexcept {
//       return static_cast<std::uint8_t>(count) == 0 || ptr != nullptr;
//   }
//     // UINT8-TRUNCATION — caught at (256, nullptr) (low byte = 0).
//
//   template <std::integral C>
//   constexpr bool valid_span(C count, const void* ptr) noexcept {
//       return (count & 0xFFu) == 0 || ptr != nullptr;
//   }
//     // BIT-MASK — caught at (256, nullptr) (count & 0xFF = 0).
//
//   template <std::integral C>
//   constexpr bool valid_span(C count, const void* ptr) noexcept {
//       return static_cast<std::int16_t>(count) == 0 || ptr != nullptr;
//   }
//     // INT16-NARROWING — for count = 65536 = 0x10000:
//     //   int16_t(0x10000) = 0 → admits.  Same low-byte-zero
//     //   class but at a different magnitude; (256, nullptr) does
//     //   NOT catch (int16_t(256) = 256, ≠ 0).  At this magnitude
//     //   class the catch is delegated to the property fuzzer
//     //   (test_decide_fuzz.cpp), which samples across the full
//     //   uint32_t domain.  Listed for completeness.
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

// (count = 256, ptr = nullptr) is the low-byte-zero magnitude
// witness that distinguishes width-narrowing-truncation from the
// small-magnitude bugs caught by the companion (count_only) fixture.
// valid_span(256, nullptr) correctly returns false (256 ≠ 0 AND
// ptr is null); CRUCIBLE_PRE's __builtin_trap fires at consteval.
// Catches the uint8_t-truncation / bit-mask-instead-of-equality
// bug classes — the predicate-confusion shape that the count_only
// fixture cannot detect because uint8_t(1) = 1 (non-zero, doesn't
// trigger the truncation path).
constexpr auto witness = gate(std::uint32_t{256}, static_cast<const void*>(nullptr));

}  // namespace

int main() { return 0; }
