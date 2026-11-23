// A unit that includes foundation/core/Text.h and not foundation/core/Region.h
// calls the door of the key of a cursor of a View.  Region.h defines that
// door, so here the door is only declared, and the unit cannot take the key
// of a cursor over any run through it.

#include <foundation/core/Text.h>

int main() {
    static_cast<void>(foundation::core::detail::ViewCursorDoor::key());
    return 0;
}
