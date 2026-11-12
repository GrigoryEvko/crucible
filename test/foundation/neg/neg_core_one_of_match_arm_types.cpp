// The arms of a match give one type, so an arm that gives a different type
// is refused.

#include <foundation/core/OneOf.h>

enum class Code : unsigned char {
    full
};

int main() {
    ::foundation::core::OneOf<int, Code> reading{3};
    return static_cast<int>(static_cast<::foundation::core::OneOf<int, Code>&&>(reading).match(
        [](int value) noexcept { return value; }, [](Code) noexcept { return 0L; }));
}
