// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Vigil::AlignmentPos is fixy::Refined<bounded_above<5>, uint8_t>, and
// Vigil::alignment_pos_at is its one door.  The door checks the bound with a
// precondition, and a failed precondition stops a constant evaluation.  This
// fixture passes 6, the smallest value past the bound, so a bound that drops
// by one or that loses its check lets this file compile.  With a value of 5
// or less, the file compiles.
//
// The sibling fixture neg_vigil_alignment_pos_uint8_max passes the largest
// byte.

#include <crucible/Vigil.h>

int main() {
    constexpr crucible::Vigil::AlignmentPos bad = crucible::Vigil::alignment_pos_at(uint8_t{6});
    (void)bad;
    return 0;
}
