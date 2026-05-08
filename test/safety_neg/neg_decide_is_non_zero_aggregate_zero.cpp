// SPDX-License-Identifier: Apache-2.0
//
// Negative-compile fixture (HS14 mandate) for crucible::decide::
// is_non_zero, mismatch class #2: AGGREGATE STRUCTURAL ZERO
// (field-myopic violator).
//
// Pins the predicate's behavior on multi-field aggregates: an
// implementation that only checks the FIRST field of the aggregate
// would pass `Aggregate{0, 0}` but COULD STILL FAIL to reject the
// canonical zero-Uuid case.  This fixture pins the all-fields-zero
// case explicitly.  CRUCIBLE_PRE fires `__builtin_trap()` at
// consteval, which the front-end rejects as "non-constant condition".
//
// Witness: `Aggregate{0, 0}`.  An aggregate with two `uint64_t`
// fields, both default-constructed to 0 — exactly the layout of
// `cog::Uuid` and many other aggregate hash / id types in the
// codebase.  An impl that fails to compare via `T{} != x` would
// COMPILE this fixture; the correct impl rejects.
//
// ───────────────────────────────────────────────────────────────────
// FIELD-MYOPIA AS A BUG CLASS
// ───────────────────────────────────────────────────────────────────
//
// The field-myopic anti-pattern arises naturally from naive
// hand-rolled "is_zero" implementations:
//
//   constexpr bool is_zero(Uuid const& u) {
//       return u.hi == 0;          // FIELD-MYOPIC: drops u.lo
//   }
//
// This passes ALL of:
//   - Uuid{0, 0}  (rejected — both zero)
//   - Uuid{1, 0}  (accepted — hi non-zero)
//   - Uuid{1, 5}  (accepted — hi non-zero)
//
// ...but WRONGLY accepts:
//   - Uuid{0, 1}  (should reject as "uninitialized + 1 in low half"
//                  is meaningful only by accident).
//
// The wrapping `is_non_zero` predicate uses the language-level
// `T{} != x` comparison, which for an aggregate with defaulted
// `operator<=>` compares ALL fields.  This fixture pins the
// all-zero case; the COMPANION (field-myopic-positive in
// test_decide.cpp's static_asserts) pins the half-zero case
// `Aggregate{0, 1}` MUST be accepted.
//
// In production this bug class manifests as: a
// `pre (decide::is_non_zero(uuid))` cite in `cog::content_hash`
// guards against zero-Uuid CogIdentity (the reserved sentinel for
// "uninitialized Cog").  A field-myopic impl that only checks
// `uuid.hi != 0` would admit `Uuid{0, k}` for any k≠0 — a
// half-uninitialized Cog identity that produces a deterministic
// but meaningless `content_hash`.  Subsequent KernelCache slot
// allocation routes ALL such half-uninitialized Cogs to the same
// slot regardless of `k`, causing kernels compiled for one Cog to
// run on another.
//
// The bug is sneaky because:
//
//   1. Both fixtures appear simple at a glance: the aggregate is
//      a 2-field struct with default-constructed fields.  A
//      reviewer might assume "it's a tiny aggregate, of course the
//      check works for all fields" — but only the all-fields check
//      catches a field-myopic impl.
//   2. A buggy impl `return x.hi != 0;` (FIELD-MYOPIC) correctly
//      REJECTS this fixture (Aggregate{0, 0} has hi == 0).  But the
//      COMPANION fixture's positive `is_non_zero(Aggregate{0, 1}) ==
//      true` static_assert in test_decide.cpp would fail under a
//      field-myopic impl (it returns false for {0, 1}).
//   3. So the negative-compile fixture pins one direction (zero
//      MUST reject), and the positive static_assert pins the OTHER
//      direction (half-zero MUST accept).  Together they fully
//      pin the predicate's behavior on aggregates.
//
// Anti-pattern targeted: field-myopic / single-field /
// first-field-only implementations of structural-zero predicates.
// Specific shapes:
//
//   constexpr bool is_non_zero(Uuid const& u) {
//       return u.hi != 0;          // FIRST-FIELD-ONLY
//   }
//     // Catches partial-uninitialization in the LOW half.  This
//     // fixture catches: Uuid{0, 0} returns false (correct), so
//     // the predicate appears to work — but the companion's
//     // is_non_zero(Aggregate{0, 1}) static_assert would fail
//     // because the impl returns false where it should return true.
//
//   constexpr bool is_non_zero(Uuid const& u) {
//       return u.hi != 0 && u.lo != 0;  // CONJUNCTION-OF-FIELDS
//   }
//     // Wrongly REJECTS Uuid{1, 0} (hi non-zero, lo zero — but
//     // the impl demands BOTH non-zero).  This fixture would NOT
//     // catch the bug (Aggregate{0,0} correctly rejects under
//     // conjunction); the positive test_decide.cpp static_assert
//     // `is_non_zero(Aggregate{0xDEADBEEF, 0}) == true` would fail.
//
//   constexpr bool is_non_zero(T const& x) {
//       return std::memcmp(&x, &T{}, sizeof(T)) != 0;
//     // SOUND but uses memcmp — works for trivially-comparable T.
//     // The issue is portability across padding bits.
//   }
//     // Sound, but the production impl uses operator!= via the
//     // requires-clause concept, which is preferable: it respects
//     // user-defined equality, no padding-bit pitfalls.
//
// Distinct from the companion fixture (integer-zero):
//   * integer-zero (companion)        — built-in `T{0}`.  Catches
//     ALWAYS-ACCEPT / INVERTED-SENSE.
//   * aggregate-zero (this fixture)   — `Aggregate{0, 0}`.  Catches
//     bug shapes that get the integer case right but mis-handle the
//     aggregate case (e.g., DEFAULT-CONSTRUCTOR-DROP that calls
//     T() and compares pointers instead of values, or
//     STRUCTURAL-FIRST-FIELD which the positive-side companion
//     witness pins).  Also serves as the canonical aggregate
//     case for `cog::Uuid` and `KernelCache` hash slots.
//
// Together the two fixtures pin TWO ORTHOGONAL bug-class buckets
// for is_non_zero: an always-accept impl fails the companion; an
// aggregate-comparison-broken impl (e.g., one that dereferences
// `&T{}` then segfaults at consteval, or that uses the wrong
// equality operator) fails this.
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "non-constant condition" / "not a constant expression" /
//   "__builtin_trap" / "call to non-constexpr function".

#include <crucible/safety/Decide.h>
#include <crucible/safety/Pre.h>

#include <compare>
#include <cstdint>

namespace {

// Mirror the cog::Uuid layout (two uint64_t fields, defaulted
// operator<=>) without depending on cog/CogIdentity.h — the fixture
// is self-contained and does not import production types.
struct Aggregate {
    std::uint64_t hi = 0;
    std::uint64_t lo = 0;
    auto operator<=>(Aggregate const&) const noexcept = default;
};

[[nodiscard]] constexpr bool gate(Aggregate const& a) noexcept {
    CRUCIBLE_PRE(crucible::decide::is_non_zero(a));
    return true;
}

// `Aggregate{0, 0}` — both fields default-constructed.  This is the
// structural-zero of the type and the canonical "uninitialized"
// sentinel for cog::Uuid, ContentHash, MerkleHash, and similar
// aggregate fingerprints across the codebase.  is_non_zero rejects;
// CRUCIBLE_PRE's __builtin_trap fires at consteval.  Catches
// aggregate-comparison-broken bug classes.
constexpr auto witness = gate(Aggregate{0, 0});

}  // namespace

int main() { return 0; }
