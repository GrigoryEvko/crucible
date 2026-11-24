// Negative-compile test. This file must fail to compile.
//
// A Quantity of a float has no ==: the == of IEEE 754 makes +0 equal to -0
// and a NaN equal to nothing.  equal of the base compares its bits, and a
// compare with a tolerance is a door of its own.
//
// Expected diagnostic: no == of two Quantities of a double.

#include <foundation/core/Quantity.h>

int main() {
    using Seconds = ::foundation::core::Quantity<::foundation::core::unit::Second, double>;
    return Seconds{1.0} == Seconds{1.0} ? 0 : 1;
}
