// offset_of counts the position of part in elements of whole.  A part of a
// different element type has no such position, so offset_of asks for one
// element type, with or without a const.

#include <foundation/core/Region.h>

int main() {
    ::foundation::core::View<int> const whole{};
    ::foundation::core::View<long> const part{};
    return static_cast<int>(::foundation::core::offset_of(whole, part).is_none());
}
