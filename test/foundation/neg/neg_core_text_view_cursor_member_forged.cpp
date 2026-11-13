// foundation/core/Text.h declares begin() of TextView and defines it in the
// class.  A unit that includes Text.h therefore cannot give the member a
// definition of its own with the access of TextView and read the private
// members of a text: the second definition is a redefinition.

#include <foundation/core/Text.h>

namespace foundation::core {

constexpr ViewCursor<char const> TextView::begin() const noexcept {
    return ViewCursor<char const>{detail::ViewCursorDoor::key(), first_ + size_, first_ + size_};
}

}  // namespace foundation::core

int main() { return 0; }
