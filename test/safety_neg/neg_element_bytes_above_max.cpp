// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: constructing ElementBytes with a value > 16 in a
// constexpr context.
//
// Per WRAP-Types-1 (#1067), ElementBytes::value_ is wrapped in
// safety::Refined<safety::bounded_above<uint8_t{16}>, uint8_t>.
// Refined's checked ctor carries `pre(Pred(v))` (P2900R14), so
// `ElementBytes{17}` triggers `bounded_above<16>(17) == false` →
// contract violation.
//
// In a constexpr context (constant evaluation), a contract violation
// makes the expression non-constant per P1494R5 — using it where a
// constant is required is ill-formed.  This pins the structural
// guarantee: a future revision that loosens the predicate (or drops
// the Refined wrap entirely, reverting to a raw uint8_t) silently
// admits ElementBytes{17}; this test fires on that drift.
//
// Value domain reminder: {0, 1, 2, 4, 8, 16}.  17 is the smallest
// out-of-domain value that fits in uint8_t — narrow, surgical, no
// overflow ambiguity.

#include <crucible/Types.h>

int main() {
    // constexpr forces constant evaluation of the ctor's pre clause.
    // bounded_above<16>(17) == false → contract violation → not a
    // constant expression → ill-formed.
    constexpr crucible::ElementBytes bad{uint8_t{17}};
    (void)bad;
    return 0;
}
