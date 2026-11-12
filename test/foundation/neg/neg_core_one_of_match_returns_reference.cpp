// The rvalue match gives up the OneOf, so an arm that gives a reference into
// its value is refused: the reference would dangle.

#include <foundation/core/OneOf.h>

enum class Code : unsigned char {
    full
};

inline constexpr int fallback = 0;
using NumberRef = int const&;

int main() {
    ::foundation::core::OneOf<int, Code> reading{3};
    return static_cast<::foundation::core::OneOf<int, Code>&&>(reading).match(
        [](int&& value) noexcept -> NumberRef { return value; }, [](Code&&) noexcept -> NumberRef { return fallback; });
}
