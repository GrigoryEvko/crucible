// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: constructing IterationDetector::MatchPos with a value > K-1 = 4
// in a constexpr context.
//
// Per WRAP-IterDet-1 (#927), IterationDetector::match_pos_ is wrapped in
// safety::Refined<safety::bounded_above<MATCH_POS_MAX>, uint8_t> where
// MATCH_POS_MAX = K - 1 = 4.  Refined's checked ctor carries
// `pre(Pred(v))` (P2900R14), so `MatchPos{5}` triggers
// `bounded_above<4>(5) == false` → contract violation.
//
// In a constexpr context (constant evaluation), a contract violation
// makes the expression non-constant per P1494R5 — using it where a
// constant is required is ill-formed.  This pins the structural
// guarantee: a future revision that loosens the predicate (or drops
// the Refined wrap entirely, reverting to a raw uint8_t) silently
// admits MatchPos{5}; this test fires on that drift.
//
// Value choice: 5 is the smallest forbidden value (= K, the boundary
// past which on_match_() resets to 0 — the value that the field is
// never SUPPOSED to hold by control flow).  This is the narrow case
// that catches off-by-one drift in MATCH_POS_MAX.

#include <crucible/ir001/IterationDetector.h>

int main() {
    // constexpr forces constant evaluation of the ctor's pre clause.
    // bounded_above<4>(5) == false → contract violation → not a
    // constant expression → ill-formed.
    constexpr crucible::IterationDetector::MatchPos bad{uint8_t{5}};
    (void)bad;
    return 0;
}
