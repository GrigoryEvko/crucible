// The frame door of an Option of a View.  The empty value of the niche of a
// View is a count that no View holds, and a write through the RefMut could
// write it, so a full Option would read as empty.  The niche of a View does
// not state that no door builds its empty value, so the door refuses the
// payload.

#include <foundation/core/Region.h>

int main() {
    ::foundation::core::Option<::foundation::core::View<int>> option{::foundation::core::none};
    option.with_mut([](auto) noexcept {}, [] noexcept {});
    return 0;
}
