// A float and a double compare by their bits.  A long double holds padding
// bits on x86_64, so its bits are no value, and it is not Equatable.  A
// Record with a long double part has no ==.

#include <foundation/core/Record.h>

int main() {
    ::foundation::core::Record<int, long double> const left{1, 2.5L};
    ::foundation::core::Record<int, long double> const right{1, 2.5L};
    return left == right ? 0 : 1;
}
