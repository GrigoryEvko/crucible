// get<T>() names an alternative of the OneOf, so a type that no alternative
// has gives no overload.

#include <foundation/core/OneOf.h>

int main() {
    ::foundation::core::OneOf<int, long> held{7};
    return held.get<short>().is_some() ? 0 : 1;
}
