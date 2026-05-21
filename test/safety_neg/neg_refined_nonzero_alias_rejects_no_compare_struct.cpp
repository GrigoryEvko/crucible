// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #1 of 2 for FIXY-U-159 NonZero<T> / NonEmpty<T> /
// NonEmptySpan<T> §XVI alias surface.
//
// Premise: `safety::NonZero<T>` is the named template alias for
// `Refined<non_zero, T>` — added to Refined.h to close the §XVI
// "every load-bearing predicate gets a named alias" discipline
// gap that previously drove production code to define ad-hoc
// local aliases (CallSiteTable.h:114 `NonZeroHash`).
//
// The alias inherits Refined's `requires PredicateInvocableOn
// <non_zero, T>` concept gate at construction.  `non_zero`'s body
// is two-branched (P2780-style requires-clause):
//
//   if constexpr (requires { x.raw(); })
//       return x.raw() != 0;
//   else
//       return x != decltype(x){0};
//
// A type that has neither `.raw()` nor `operator!=` against a
// default-constructed value of its own type falls through both
// branches with substitution failures.  The concept gate fires
// at the alias instantiation, NOT a wall of SFINAE deep inside
// the contract's `pre(non_zero(v))` clause.
//
// Distinct mismatch class from companion fixture
// neg_refined_nonempty_alias_rejects_arithmetic.cpp:
//   * This fixture: non_zero predicate's else-branch fails because
//     the type has no operator!= with literal-zero — the FALLBACK
//     branch of an if-constexpr-requires dispatch.
//   * Companion:    non_empty predicate's lone `.empty()` member
//     requirement fails — a SINGLE-BRANCH method-missing failure.
// Two structurally different concept-failure surfaces in the
// non_zero / non_empty lambda families.
//
// Substring "PredicateInvocableOn" pins the diagnostic — GCC 16
// emits the concept name in the "constraint requires" line of
// the `NonZero<NoCompareStruct>{...}` construction site.
//
// FIXY-U-159 first HS14 fixture closing the §XVI alias discipline
// gap (alongside the new NonEmpty / NonEmptySpan aliases).

#include <crucible/safety/Refined.h>

namespace {

// A struct with neither `.raw()` accessor nor `operator!=` defined.
// non_zero(x) tries the requires-branch — no .raw() — and falls
// through to `x != decltype(x){0}`.  Neither operator!= nor
// decltype(x){0} (ctor-from-int) is available, so the lambda
// body fails substitution.  PredicateInvocableOn<non_zero,
// NoCompareStruct> is therefore false at the concept boundary.
struct NoCompareStruct {
    int payload = 0;
    // Note: NO operator!= overload.
    // Note: NO converting constructor from int.
};

}  // namespace

int main() {
    using crucible::safety::NonZero;

    // VIOLATION: NonZero<T> = Refined<non_zero, T>; ctor's
    // `requires PredicateInvocableOn<non_zero, NoCompareStruct>`
    // is false → concept-violation diagnostic at this line.
    NonZero<NoCompareStruct> bad{NoCompareStruct{}};
    (void)bad;
    // ERROR: no matching constructor; constraints not satisfied —
    // 'PredicateInvocableOn' [with auto Pred = non_zero, T = NoCompareStruct]
    return 0;
}
