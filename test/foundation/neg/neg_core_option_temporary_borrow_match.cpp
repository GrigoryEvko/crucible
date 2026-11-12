// A temporary Option binds to the deleted const&& match, and not to the
// borrow match, so an arm cannot give a reference into the temporary.

#include <foundation/core/Choice.h>

inline constexpr int fallback = 0;
using NumberRef = int const&;

int main() {
    NumberRef seen = ::foundation::core::Option<int>::some(3).match(
        [](int const& value) noexcept -> NumberRef { return value; }, []() noexcept -> NumberRef { return fallback; });
    return seen;
}
