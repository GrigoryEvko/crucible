// The == of a floating value is not an equality, because a NaN differs from
// itself.  So a Record with a floating part has no ==, and it compares
// through <=>.

#include <foundation/core/Record.h>

int main() {
    ::foundation::core::Record<int, double> const left{1, 2.5};
    ::foundation::core::Record<int, double> const right{1, 2.5};
    return left == right ? 0 : 1;
}
