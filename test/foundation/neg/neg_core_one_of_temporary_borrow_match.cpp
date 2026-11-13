// A const rvalue of a OneOf binds to the deleted const&& match, and not to
// the borrow match, so an arm cannot give a reference into an object that the
// full expression ends.  A non-const rvalue binds to the rvalue match, which
// takes each arm by value.

#include <foundation/core/OneOf.h>

enum class Code : unsigned char {
    full
};

inline constexpr int fallback = 0;
using NumberRef = int const&;

int main() {
    ::foundation::core::OneOf<int, Code> const held{3};
    NumberRef seen = static_cast<::foundation::core::OneOf<int, Code> const&&>(held).match(
        [](int const& value) noexcept -> NumberRef { return value; },
        [](Code const&) noexcept -> NumberRef { return fallback; });
    return seen;
}
