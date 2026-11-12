// A match takes one arm for each alternative, so a call that forgets an
// alternative does not compile.

#include <foundation/core/OneOf.h>

enum class Code : unsigned char {
    full
};

int main() {
    ::foundation::core::OneOf<int, Code, long> reading{3};
    return static_cast<::foundation::core::OneOf<int, Code, long>&&>(reading).match(
        [](int value) noexcept { return value; }, [](Code) noexcept { return 0; });
}
