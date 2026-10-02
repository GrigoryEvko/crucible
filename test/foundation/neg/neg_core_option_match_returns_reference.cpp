// The rvalue match moves the payload into the match, so the payload ends
// when the match returns.  An arm that gives a reference to the payload
// would give a dangling reference, so the match refuses a reference result.

#include <foundation/core/Choice.h>

inline int fallback_value = 0;

int main() {
    int&& kept = ::foundation::core::Option<int>::some(3).match(
        [](int&& value) noexcept -> int&& { return static_cast<int&&>(value); },
        []() noexcept -> int&& { return static_cast<int&&>(fallback_value); });
    return kept;
}
