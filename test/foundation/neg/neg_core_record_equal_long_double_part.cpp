// A float and a double compare by their bits.  A long double holds padding
// bits on x86_64, so its bits are no value, and it is not Equatable.  A
// Record with a long double part has no ==.  The compare takes the two
// Records by reference, so the fixture has no expression of type long
// double, which the rule quarantine counts outside the base.

#include <foundation/core/Record.h>

inline bool is_equal(::foundation::core::Record<int, long double> const& left,
                     ::foundation::core::Record<int, long double> const& right) {
    return left == right;
}

int main() { return 0; }
