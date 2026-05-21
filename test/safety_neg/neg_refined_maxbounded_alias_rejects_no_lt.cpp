// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #3 of 4 for FIXY-U-160 MinLength<N, T> /
// MaxBounded<Max, T> §XVI parameterised-alias surface.
//
// Premise: `safety::MaxBounded<Max, T>` is the named template alias
// for `Refined<bounded_above<Max>, T>` — closes the §XVI alias-
// discipline gap for the *parameterised* upper-bound family.  The
// alias inherits Refined's `requires PredicateInvocableOn<
// bounded_above<Max>, T>` concept gate at construction.
//
// `bounded_above<Max>(x)` is defined as `x < Max` (lambda body in
// safety/Refined.h:343).  A type without `operator<(decltype(Max))` —
// a struct with neither operator< nor implicit conversion to a
// comparable type — fails the substitution because `x < Max` is not
// a valid expression.  PredicateInvocableOn<bounded_above<Max>,
// NoLtStruct> is therefore false.
//
// Mismatch class for this fixture (distinct from companion fixtures):
//   * THIS fixture (Class M — operator< missing on user struct):
//     the parameterised predicate's body `x < Max` substitution fails
//     because T has no operator< that accepts Max's deduced type.
//     This mirrors U-159's neg_refined_nonzero_alias_rejects_no_
//     compare_struct.cpp fixture but for the bounded_above family —
//     where the canonical production-site misuse is a freshly-
//     defined struct that hasn't yet been given the comparison
//     operators its containing aggregate needs.
//   * Companions #1 and #2: MinLength fixtures (Class C arithmetic
//     and Class N nested-Refined).
//   * Companion #4 (Class T — heterogeneous comparison): T is a
//     non-numeric class (std::string) and Max is an integer NTTP —
//     `string < int{10}` is an ill-formed comparison.
//
// Substring matched: "no match for" (rather than PredicateInvocableOn).
// Reason: bounded_above<Max>'s operator() body unconditionally evaluates
// `x <= decltype(x)(Max)` with NO if-constexpr-requires guard branch.
// That means the rejection surfaces at constexpr-expansion time as
// `no match for 'operator<='` rather than at concept-gate time as
// PredicateInvocableOn-false.  Both are legitimate-rejection
// diagnostics — the discipline this fixture witnesses is that the
// predicate body's structural requirement on T does fire as a compile
// error at the construction site, by whichever route GCC reaches it.

#include <crucible/safety/Refined.h>

namespace {

// A struct with no operator< overload and no implicit conversion to a
// type with operator<.  bounded_above<Max>(s) evaluates `s < Max`,
// which fails substitution.  Distinct from U-159's NoCompareStruct
// (which targeted operator!=); here we target operator< specifically
// because the bounded_above predicate family has a strictly-less-than
// upper-bound semantics, NOT an inequality semantics.
struct NoLtStruct {
    int payload = 0;
    // Note: NO operator< overload.
    // Note: NO converting constructor from int.
};

}  // namespace

int main() {
    using crucible::safety::MaxBounded;

    // VIOLATION: MaxBounded<10, NoLtStruct> = Refined<bounded_above<10>,
    // NoLtStruct>; ctor's `requires PredicateInvocableOn<
    // bounded_above<10>, NoLtStruct>` is false because NoLtStruct has
    // no operator< — concept-violation diagnostic at this line, NOT a
    // SFINAE wall inside <contracts>.
    MaxBounded<10, NoLtStruct> bad{NoLtStruct{}};
    (void)bad;
    // ERROR: no matching constructor; constraints not satisfied —
    // 'PredicateInvocableOn' [with auto Pred = bounded_above<10>,
    //                         T = NoLtStruct]
    return 0;
}
