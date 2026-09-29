// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: ElementBytes receives a value above 16 in constant evaluation.
//
// ElementBytes::value_ is ::fixy::Refined<::fixy::bounded_above<uint8_t{16}>,
// uint8_t>.  The constructor states the bound as a contract assertion ahead
// of the checked mint, so ElementBytes{17} violates bounded_above<16> twice.
// In constant evaluation a contract violation makes the expression not a
// constant expression, so the constexpr variable is ill-formed.  A revision
// that loosens the bound, or drops the refinement for a raw uint8_t, admits
// ElementBytes{17}, and this fixture fires.
//
// The value domain is {0, 1, 2, 4, 8, 16}.  17 is the smallest value outside
// it that fits in uint8_t.

#include <crucible/Types.h>

int main() {
    constexpr crucible::ElementBytes bad{uint8_t{17}};
    (void)bad;
    return 0;
}
