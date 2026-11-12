// A value converts only to the alternative of its own type, so a value of
// a different type does not select an alternative through a conversion.

#include <foundation/core/OneOf.h>

int main() {
    ::foundation::core::OneOf<int, long> const reading{5u};
    return reading.is<int>() ? 0 : 1;
}
