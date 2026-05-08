// SPDX-License-Identifier: Apache-2.0
//
// Negative-compile fixture (HS14 mandate) for crucible::decide::
// is_non_zero, mismatch class #1: BUILT-IN INTEGRAL ZERO
// (always-true / unconditional-accept violator).
//
// Pins the predicate's defining clause for the simplest possible
// witness: a built-in integer with literal value 0.  CRUCIBLE_PRE
// fires `__builtin_trap()` at consteval, which the front-end rejects
// as "non-constant condition".
//
// Witness: `std::uint64_t{0}`.  The structural-zero value of every
// built-in integer.  A predicate that returned `true` unconditionally
// (or that accidentally inverted its sense via a typo `==` / `!=`)
// would COMPILE this fixture; the correct impl rejects.
//
// In production this bug class manifests as: a contract guard
// `pre (decide::is_non_zero(content_hash))` ought to reject the
// reserved zero-sentinel that means "slot empty" in KernelCache,
// "head not yet advanced" in Cipher, "uninitialized RegionNode" in
// MerkleDag.  An accept-zero implementation admits the sentinel,
// causing the consumer to:
//
//   - KernelCache: read a "valid" slot containing a zero hash, then
//     dispatch to the corresponding bucket (bucket 0), pulling out
//     the first kernel that landed at that bucket regardless of
//     content-hash equality — silent wrong-kernel execution.
//   - Cipher: append a zero-head LogEntry, breaking the chain
//     invariant where each entry's prev_hash references the previous
//     entry's content_hash; replay sees a valid-looking chain but the
//     chain is corrupted at the first zero-link.
//   - MerkleDag: equate two distinct uninitialized RegionNodes via
//     their content_hash, deduping them at construction even though
//     no actual structural equivalence holds.
//
// The bug is sneaky because:
//
//   1. Zero is the most common default value across the codebase.
//      An implementation that "checks if the value is meaningful"
//      via `return x;` (truthiness test in C, always non-zero in
//      C++ proper for non-bool integers) would happen to work for
//      `0` (returns false correctly) but accidentally accept any
//      non-zero value as "true" — looks correct on a quick read.
//      The bug class targeted here is the OTHER direction: a
//      `return true;` or `return !(x == 0);` typo that always
//      accepts.
//   2. A buggy impl `return true;` (always-accept) would COMPILE
//      this fixture; the correct impl rejects.
//   3. A buggy impl `return x == T{};` (inverted sense) would also
//      COMPILE this fixture; the correct impl rejects.
//   4. The companion fixture (aggregate zero) does NOT catch THIS
//      bug class directly: an impl that ALWAYS returns true would
//      accept both fixtures.  However, the companion catches a
//      DIFFERENT bug — the field-myopic implementation that only
//      checks the first field — by using a witness where the
//      first field is zero but a later field is non-zero.  Both
//      fixtures together pin orthogonal bug-class buckets.
//
// Anti-pattern targeted: always-accept / inverted-sense / no-op
// implementations of is_non_zero.  Specific shapes:
//
//   template <typename T>
//   constexpr bool is_non_zero(T const&) { return true; }
//     // ALWAYS-ACCEPT — every built-in zero passes.  This fixture
//     // catches it: T{0} is structurally the zero value and must
//     // reject.
//
//   template <typename T>
//   constexpr bool is_non_zero(T const& x) { return x == T{}; }
//     // INVERTED-SENSE — accepts exactly the values that should
//     // reject.  This fixture catches it: x == T{} holds for
//     // x = T{0}, so the inverted predicate "accepts" zero.
//
//   template <typename T>
//   constexpr bool is_non_zero(T const&) { return T{} != T{}; }
//     // PATHOLOGICAL — checks if the default-constructed value
//     // is non-default-constructed (always false).  Returns false
//     // for everything.  This fixture would PASS such an impl
//     // (false rejects T{0}, which is correct) but the COMPANION
//     // (aggregate non-zero accepted) would catch it.
//
// Distinct from the companion fixture (aggregate first-field-zero):
//   * integer-zero (this fixture)     — built-in T{0}.  Catches
//     ALWAYS-ACCEPT / INVERTED-SENSE.  Also serves as the simplest
//     possible witness.
//   * aggregate-zero (companion)      — `Mock{0, 1}` (first field
//     zero, second field non-zero).  Catches FIELD-MYOPIC impls
//     that only test the first structural slot.
//
// Together the two fixtures pin TWO ORTHOGONAL bug-class buckets
// for is_non_zero: an always-accept impl fails this; a field-myopic
// impl fails the companion.
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "non-constant condition" / "not a constant expression" /
//   "__builtin_trap" / "call to non-constexpr function".

#include <crucible/safety/Decide.h>
#include <crucible/safety/Pre.h>

#include <cstdint>

namespace {

template <typename T>
[[nodiscard]] constexpr bool gate(T const& x) noexcept {
    CRUCIBLE_PRE(crucible::decide::is_non_zero(x));
    return true;
}

// `std::uint64_t{0}` — the structural zero of the canonical 64-bit
// unsigned integer.  is_non_zero rejects; CRUCIBLE_PRE's
// __builtin_trap fires at consteval.  Catches always-accept and
// inverted-sense bug classes that would silently admit the
// reserved zero sentinel at every cite.
constexpr auto witness = gate(std::uint64_t{0});

}  // namespace

int main() { return 0; }
