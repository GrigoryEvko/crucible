// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: constructing Vigil::AlignmentPos with a value > ALIGNMENT_K = 5
// in a constexpr context.
//
// Per WRAP-Vigil-7 (#1077), Vigil::alignment_pos_ is wrapped in
// safety::Refined<safety::bounded_above<ALIGNMENT_POS_MAX>, uint8_t> where
// ALIGNMENT_POS_MAX = ALIGNMENT_K = 5.  Refined's checked ctor carries
// `pre(Pred(v))` (P2900R14), so `AlignmentPos{6}` triggers
// `bounded_above<5>(6) == false` → contract violation.
//
// In a constexpr context (constant evaluation), a contract violation
// makes the expression non-constant per P1494R5 — using it where a
// constant is required is ill-formed.  This pins the structural
// guarantee: a future revision that loosens the predicate (or drops
// the Refined wrap entirely, reverting to a raw uint32_t) silently
// admits AlignmentPos{6}; this test fires on that drift.
//
// Value choice: 6 is the smallest forbidden value (= ALIGNMENT_K + 1,
// the off-by-one past the threshold check at try_align_'s `>= threshold`
// branch — the value that the field is never SUPPOSED to hold by
// control flow).  This is the narrow case that catches off-by-one
// drift in ALIGNMENT_POS_MAX (e.g., a refactor that lowers the bound
// to ALIGNMENT_K - 1 silently passes the wide-miss fixture but fails
// this one).

#include <crucible/Vigil.h>

int main() {
    // constexpr forces constant evaluation of the ctor's pre clause.
    // bounded_above<5>(6) == false → contract violation → not a
    // constant expression → ill-formed.
    constexpr crucible::Vigil::AlignmentPos bad{uint8_t{6}};
    (void)bad;
    return 0;
}
