// A class that is not a friend of the door of the key of a cursor of a text
// takes the name of TextView in its own namespace, and calls the door to
// build a cursor over any characters.  The friend is
// foundation::core::TextView, so the member of the door is private here.

#include <foundation/core/Text.h>

namespace lookalike {

class TextView {
public:
    [[nodiscard]] static foundation::core::ViewCursor<char const> forge() noexcept {
        return foundation::core::ViewCursor<char const>{foundation::core::detail::TextCursorDoor::key(),
                                                        static_cast<char const*>(nullptr),
                                                        static_cast<char const*>(nullptr)};
    }
};

}  // namespace lookalike

int main() {
    foundation::core::ViewCursor<char const> const cursor = lookalike::TextView::forge();
    return cursor.is_at_end() ? 0 : 1;
}
