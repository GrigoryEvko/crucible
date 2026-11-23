// A float and a double payload compare by their bits.  A long double holds
// padding bits on x86_64, so its bits are no value, and it is not Equatable.
// An Option of a long double has no compare.  The two Options hold no value,
// so the fixture has no expression of type long double, which the rule
// quarantine counts outside the base.

#include <foundation/core/Choice.h>

int main() {
    ::foundation::core::Option<long double> const left{::foundation::core::none};
    ::foundation::core::Option<long double> const right{::foundation::core::none};
    return left == right ? 0 : 1;
}
