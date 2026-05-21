// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #2 of 2 for FIXY-U-159 NonZero<T> / NonEmpty<T> /
// NonEmptySpan<T> §XVI alias surface.
//
// Premise: `safety::NonEmpty<T>` is the named template alias for
// `Refined<non_empty, T>` — closes the §XVI alias discipline gap
// for container-shape "non-emptiness".  Distinct from
// `NonEmptySpan<T>` which is span-specific via `length_ge<1>`
// (the §XVI-cited canonical alias).
//
// The alias inherits Refined's `requires PredicateInvocableOn
// <non_empty, T>` concept gate at construction.  `non_empty`'s
// body is the simplest possible container shape:
//
//   inline constexpr auto non_empty = [](const auto& c) constexpr
//       noexcept { return !c.empty(); };
//
// A type without an `.empty()` member function — arithmetic
// types being the most common production-site misuse — fails
// the substitution because `c.empty()` is not a valid
// expression on `int const&`.  PredicateInvocableOn<non_empty,
// int> is therefore false.
//
// Distinct mismatch class from companion fixture
// neg_refined_nonzero_alias_rejects_no_compare_struct.cpp:
//   * Companion:    non_zero predicate's else-branch fails because
//     the type has no operator!= with literal-zero — the FALLBACK
//     branch of an if-constexpr-requires dispatch.
//   * This fixture: non_empty predicate's lone `.empty()` member
//     requirement fails — a SINGLE-BRANCH method-missing failure.
// Two structurally different concept-failure surfaces in the
// non_zero / non_empty lambda families.
//
// Substring "PredicateInvocableOn" pins the diagnostic — GCC 16
// emits the concept name in the "constraint requires" line of
// the `NonEmpty<int>{...}` construction site.
//
// Why arithmetic-rejected matters in practice: a confused caller
// who reaches for "non-empty integer" (perhaps thinking of
// "non-default") would naturally type `NonEmpty<int>`.  The
// alias must reject at the concept boundary, not silently fall
// through to `Refined<non_empty, int>` and emit a wall of SFINAE
// inside the contract's `pre(non_empty(v))` clause.  The named
// alias preserves the clean concept-violation diagnostic.
//
// FIXY-U-159 second HS14 fixture closing the §XVI alias
// discipline gap (alongside NonZero / NonEmptySpan).

#include <crucible/safety/Refined.h>

int main() {
    using crucible::safety::NonEmpty;

    // VIOLATION: NonEmpty<T> = Refined<non_empty, T>; ctor's
    // `requires PredicateInvocableOn<non_empty, int>` is false
    // because int has no .empty() member — concept-violation
    // diagnostic at this line, NOT a SFINAE wall inside <contracts>.
    NonEmpty<int> bad{42};
    (void)bad;
    // ERROR: no matching constructor; constraints not satisfied —
    // 'PredicateInvocableOn' [with auto Pred = non_empty, T = int]
    return 0;
}
