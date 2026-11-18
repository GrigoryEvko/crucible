// A float and a double payload compare by their bits.  A long double holds
// padding bits on x86_64, so its bits are no value, and it is not Equatable.
// An Option of a long double has no compare.

#include <foundation/core/Choice.h>

int main() {
    auto const left = ::foundation::core::Option<long double>::some(0.5L);
    auto const right = ::foundation::core::Option<long double>::some(0.5L);
    return left == right ? 0 : 1;
}
