// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #4 of 4 for FIXY-U-160 MinLength<N, T> /
// MaxBounded<Max, T> §XVI parameterised-alias surface.
//
// Premise: `MaxBounded<Max, T>` is `Refined<bounded_above<Max>, T>`.
// `bounded_above<Max>(x)` is `x < Max`.  When T is a non-numeric
// class type (e.g. std::string) and Max is an integer NTTP (e.g.
// 10u), the comparison `std::string < unsigned{10}` is an ill-formed
// expression — std::string has no operator< with unsigned int (only
// homogeneous string-vs-string ordering).  PredicateInvocableOn<
// bounded_above<10u>, std::string> is therefore false.
//
// Mismatch class for this fixture (distinct from companion fixtures):
//   * Companions #1-#3: Class C (MinLength arithmetic), Class N
//     (MinLength nested Refined), Class M (MaxBounded missing op<).
//   * THIS fixture (Class T — heterogeneous comparison): T's
//     operator< overload set does NOT contain a candidate for
//     comparing against the Max NTTP's deduced type.  std::string IS
//     ordered (has op< against std::string), so a naïve concept
//     check might pass "T is ordered" — but the specific cross-type
//     comparison `string < unsigned` is what bounded_above requires,
//     and that comparison is ill-formed.  The discipline this
//     witnesses: PredicateInvocableOn is structural at the EXACT
//     comparison the lambda body uses, not at a coarser "T has
//     some operator<" trait.
//
// This is the production-site misuse pattern where a developer
// thinks "MaxBounded means upper-bounded — strings have a max length,
// so MaxBounded<10, std::string> should mean 'string with at most 10
// characters'".  WRONG — MaxBounded uses bounded_above which is
// VALUE-LESS-THAN, not LENGTH-LESS-THAN.  The correct alias for
// "string with at most 10 characters" would be a hypothetical
// `MaxLength<10, std::string>` backed by `length_le<10>` (analogous
// to length_ge<>).  The compile error here surfaces the conceptual
// mismatch at the alias-site instead of at a runtime contract-check.
//
// Substring matched: "no match for" (rather than PredicateInvocableOn,
// for the same reason as companion #3 — bounded_above's operator()
// body is unconditional, so rejection surfaces at constexpr-expansion
// rather than at concept-gate time).  Both routes legitimately abort
// the build with the predicate body's T-requirement as the cause.

#include <crucible/safety/Refined.h>

#include <string>

int main() {
    using crucible::safety::MaxBounded;

    // VIOLATION: bounded_above<10u>(s) evaluates `s < 10u`, but
    // std::string has no operator< accepting unsigned — ill-formed
    // comparison.  PredicateInvocableOn<bounded_above<10u>, string>
    // is false → ctor's requires-clause rejects.
    MaxBounded<10u, std::string> bad{std::string{"hello"}};
    (void)bad;
    // ERROR: no matching constructor; constraints not satisfied —
    // 'PredicateInvocableOn' [with auto Pred = bounded_above<10u>,
    //                         T = std::string]
    return 0;
}
