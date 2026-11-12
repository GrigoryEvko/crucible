// expect names an alternative, so a type outside the alternatives is
// refused: the unwrap would always end the process.

#include <foundation/core/OneOf.h>

int main() {
    ::foundation::core::OneOf<int, long> reading{3};
    return static_cast<::foundation::core::OneOf<int, long>&&>(reading).expect<short>("a short") > 0 ? 1 : 0;
}
