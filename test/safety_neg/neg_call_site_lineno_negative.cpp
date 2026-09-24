// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: constructing CallSiteTable::Lineno with -1 in a
// constexpr context.
//
// Per WRAP-CallSite-2 (#880), CallSiteTable::Lineno is
// safety::NonNegative<int32_t> = safety::Refined<safety::non_negative,
// int32_t>.  Refined's checked ctor carries `pre(non_negative(v))`
// (P2900R14), so `Lineno{-1}` triggers `non_negative(-1) == false` →
// contract violation.
//
// In a constexpr context (constant evaluation), a contract violation
// makes the expression non-constant per P1494R5 — using it where a
// constant is required is ill-formed.  This pins the structural
// guarantee: a future revision that loosens the predicate (or drops
// the Refined wrap entirely, reverting to a raw int32_t) silently
// admits Lineno{-1}; this test fires on that drift.
//
// Value choice: -1 is the smallest forbidden value (boundary edge,
// = 0 - 1).  Catches off-by-one drift in the predicate ("> 0" instead
// of "≥ 0", which would falsely admit -1 == 0 - 1 if the comparison
// got rewritten).  Companion fixture (_int_min) covers wide miss.

#include <crucible/CallSiteTable.h>

int main() {
    // constexpr forces constant evaluation of the ctor's pre clause.
    // non_negative(-1) == false → contract violation → not a
    // constant expression → ill-formed.
    constexpr crucible::CallSiteTable::Lineno bad{int32_t{-1}};
    (void)bad;
    return 0;
}
