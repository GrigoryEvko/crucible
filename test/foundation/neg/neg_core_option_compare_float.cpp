// A floating-point payload has no compare: NaN is not equal to itself, and
// -Werror=float-equal refuses == of two floats.

#include <foundation/core/Choice.h>

int main() {
    auto const left = ::foundation::core::Option<double>::some(0.5);
    auto const right = ::foundation::core::Option<double>::some(0.5);
    return left == right ? 0 : 1;
}
