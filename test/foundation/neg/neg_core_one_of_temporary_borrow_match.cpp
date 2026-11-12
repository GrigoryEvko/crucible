// A temporary OneOf binds to the deleted const&& match, and not to the
// borrow match, so an arm cannot give a reference into the temporary.

#include <foundation/core/OneOf.h>

enum class Code : unsigned char {
    full
};

inline constexpr int fallback = 0;
using NumberRef = int const&;

int main() {
    NumberRef seen =
        ::foundation::core::OneOf<int, Code>{3}.match([](int const& value) noexcept -> NumberRef { return value; },
                                                      [](Code const&) noexcept -> NumberRef { return fallback; });
    return seen;
}
