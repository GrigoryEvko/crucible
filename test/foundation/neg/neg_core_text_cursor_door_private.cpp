// A unit of the base that is not TextView calls the door of the key of a
// cursor of a text, and builds a cursor over any characters that it names.
// TextView is the friend of the door, so its member is private here.

#include <foundation/core/Text.h>

int main() {
    foundation::core::ViewCursor<char const> const cursor{foundation::core::detail::TextCursorDoor::key(),
                                                          static_cast<char const*>(nullptr),
                                                          static_cast<char const*>(nullptr)};
    return cursor.is_at_end() ? 0 : 1;
}
