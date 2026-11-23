// A unit of the base that is not View or ViewMut calls the door of the key of
// a cursor, and builds a cursor over any run that it names.  View and ViewMut
// are the friends of the door, so its member is private here.

#include <foundation/core/Region.h>

int main() {
    foundation::core::ViewCursor<int> const cursor{foundation::core::detail::ViewCursorDoor::key(),
                                                   static_cast<int*>(nullptr), static_cast<int*>(nullptr)};
    return cursor.is_at_end() ? 0 : 1;
}
