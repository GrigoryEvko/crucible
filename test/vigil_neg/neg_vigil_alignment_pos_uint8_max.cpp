// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Vigil::AlignmentPos is fixy::Refined<bounded_above<5>, uint8_t>, and
// Vigil::alignment_pos_at is its one door.  The door checks the bound with a
// precondition, and a failed precondition stops a constant evaluation.  This
// fixture passes 255, the largest byte.  A wider counter cast to a byte can
// produce that value, and the door must refuse it.  With a value of 5 or
// less, the file compiles.
//
// The sibling fixture neg_vigil_alignment_pos_above_max passes the smallest
// value past the bound.

#include <crucible/Vigil.h>

int main() {
    constexpr crucible::Vigil::AlignmentPos bad = crucible::Vigil::alignment_pos_at(uint8_t{255});
    (void)bad;
    return 0;
}
