// An arm of the frame door of a Result opens a second frame of the same
// Result.  The second RefMut would be a second writer of the value beside
// the first one.  The rule borrow-match refuses each call of a member that is
// not const of the receiver in an arm.

#include <foundation/core/Choice.h>
#include <foundation/core/Region.h>

int main() {
    ::foundation::core::Result<int, ::foundation::core::LengthMismatch> result = 1;
    result.with_mut(
        [&result](auto value) noexcept {
            result.with_mut([](auto inner) noexcept { *inner += 1; }, [](auto) noexcept {});
            *value += 1;
        },
        [](auto) noexcept {});
    return 0;
}
