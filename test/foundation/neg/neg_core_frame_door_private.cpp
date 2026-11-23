// A unit of the base that is no friend of the door of a frame key builds a
// ViewMut of a brand that it names over a run of its own, and reads a Ref of
// that brand from it.  The friends of the door are the door classes of the
// owners above Frame.h, so its member is private here.

#include <foundation/core/List.h>

struct NamedBrand {};

int main() {
    foundation::core::FixedList<int, 4> list{};
    foundation::core::View<int, 4> const view = list.view().window<4>(0).expect("the list lends four slots");
    return foundation::core::ViewMut<int, NamedBrand, 4>{foundation::core::detail::FrameDoor::key(), view}
        .get(0)
        .is_some();
}
