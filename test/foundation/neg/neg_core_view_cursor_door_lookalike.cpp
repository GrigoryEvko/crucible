// A class that is not a friend of the door of the key of a cursor takes the
// name and the shape of View in its own namespace, and calls the door to
// build a cursor over any run.  The friend is foundation::core::View, so the
// member of the door is private here.

#include <foundation/core/Region.h>

#include <cstddef>

namespace lookalike {

template <class T, std::size_t Extent>
class View {
public:
    [[nodiscard]] static foundation::core::ViewCursor<T> forge() noexcept {
        return foundation::core::ViewCursor<T>{foundation::core::detail::ViewCursorDoor::key(),
                                               static_cast<T*>(nullptr), static_cast<T*>(nullptr) + Extent};
    }
};

}  // namespace lookalike

int main() {
    foundation::core::ViewCursor<int> const cursor = lookalike::View<int, 0>::forge();
    return cursor.is_at_end() ? 0 : 1;
}
