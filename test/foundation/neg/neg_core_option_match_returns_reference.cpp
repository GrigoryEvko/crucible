// The rvalue match moves the payload into the match, so the payload ends
// when the match returns.  A reference in the result would dangle when it
// names the payload, so the match refuses each reference result.

#include <foundation/core/Choice.h>

inline int fallback_value = 0;

int main() {
    int&& kept = ::foundation::core::Option<int>::some(3).match(
        [](int) noexcept -> int&& { return static_cast<int&&>(fallback_value); },
        []() noexcept -> int&& { return static_cast<int&&>(fallback_value); });
    return kept;
}
