// A OneOf has no empty state, so it has no default constructor.

#include <foundation/core/OneOf.h>

int main() {
    ::foundation::core::OneOf<int, long> const reading{};
    return reading.is<int>() ? 0 : 1;
}
