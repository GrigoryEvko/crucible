// offset_of(whole, element) gives the position of an element of whole.  A
// temporary, also the result of a conversion, is no element of a View, so it
// has no position, and offset_of refuses it.

#include <foundation/core/Region.h>

int main() {
    ::foundation::core::View<int> const whole{};
    return static_cast<int>(::foundation::core::offset_of(whole, 3).is_none());
}
