// An arm of the frame door of an Option copies its RefMut.  The copy would be
// a second writer of the payload beside the first one, so a RefMut has no
// copy.

#include <foundation/core/Choice.h>

int main() {
    ::foundation::core::Option<int> option = ::foundation::core::some(1);
    option.with_mut(
        [](auto value) noexcept {
            auto second = value;
            *second += 1;
        },
        [] noexcept {});
    return 0;
}
