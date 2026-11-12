// is names an alternative, so a type outside the alternatives is refused:
// the answer would always be false.

#include <foundation/core/OneOf.h>

int main() {
    ::foundation::core::OneOf<int, long> const reading{3};
    return reading.is<short>() ? 1 : 0;
}
