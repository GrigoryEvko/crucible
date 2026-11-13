// The rvalue match gives up the OneOf, so a reference in its result would
// dangle when it names the value.  The match refuses each reference result.

#include <foundation/core/OneOf.h>

enum class Code : unsigned char {
    full
};

inline constexpr int fallback = 0;
using NumberRef = int const&;

int main() {
    ::foundation::core::OneOf<int, Code> reading{3};
    return static_cast<::foundation::core::OneOf<int, Code>&&>(reading).match(
        [](int) noexcept -> NumberRef { return fallback; }, [](Code) noexcept -> NumberRef { return fallback; });
}
