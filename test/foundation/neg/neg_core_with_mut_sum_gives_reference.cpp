// An arm of the frame door of an Option gives a reference into the payload.
// The reference would outlive the frame, and a later write of the Option can
// end the payload that it names.  The result of a frame door holds no borrow.

#include <foundation/core/Choice.h>

int main() {
    ::foundation::core::Option<int> option = ::foundation::core::some(1);
    int const fallback = 0;
    int const& kept = option.with_mut([](auto value) noexcept -> int const& { return *value; },
                                      [&fallback]() noexcept -> int const& { return fallback; });
    return kept;
}
