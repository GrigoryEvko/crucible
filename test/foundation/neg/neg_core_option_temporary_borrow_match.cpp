// A const rvalue of an Option binds to the deleted const&& match, and not to
// the borrow match, so an arm cannot give a reference into an object that the
// full expression ends.  A non-const rvalue binds to the rvalue match, which
// takes each arm by value.

#include <foundation/core/Choice.h>

inline constexpr int fallback = 0;
using NumberRef = int const&;

int main() {
    ::foundation::core::Option<int> const held = ::foundation::core::Option<int>::some(3);
    NumberRef seen = static_cast<::foundation::core::Option<int> const&&>(held).match(
        [](int const& value) noexcept -> NumberRef { return value; }, []() noexcept -> NumberRef { return fallback; });
    return seen;
}
