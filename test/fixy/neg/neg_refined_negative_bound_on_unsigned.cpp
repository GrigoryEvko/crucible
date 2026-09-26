// A ceiling of -1 is not defined on an unsigned value.  Converted to the
// value type the ceiling would be the largest unsigned value, and every
// value would pass.  The checked mint refuses the pair at its gate.

#include <fixy/Refined.h>

int main() {
    auto refused = fixy::mint_refined<fixy::bounded_above<-1>>(4000000000u);
    return static_cast<int>(refused.value() & 1u);
}
