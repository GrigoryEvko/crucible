// A class that takes the name of the door class of Region.h in its own
// namespace calls the door of a frame key, and builds a ViewMut of a brand
// that its caller names.  The friend is foundation::core::RegionFrameKeyDoor,
// so the member of the door is private here.

#include <foundation/core/List.h>

namespace lookalike {

class RegionFrameKeyDoor final {
public:
    template <class Brand>
    [[nodiscard]] static foundation::core::ViewMut<int, Brand, 4> lend(foundation::core::View<int, 4> view) noexcept {
        return foundation::core::ViewMut<int, Brand, 4>{foundation::core::detail::FrameDoor::key(), view};
    }
};

}  // namespace lookalike

struct NamedBrand {};

int main() {
    foundation::core::View<int, 4> const view =
        foundation::core::View<int>{}.window<4>(0).expect("the View lends four elements");
    return lookalike::RegionFrameKeyDoor::lend<NamedBrand>(view).get(0).is_some();
}
