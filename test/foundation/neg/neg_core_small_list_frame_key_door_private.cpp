// A unit of the base that is no friend of the door of SmallList.h calls its
// member, and builds a ViewMut of a brand that it names over a run of its own.
// The frame door of a SmallList is the one friend of the door, so its member
// is private here.

#include <foundation/core/List.h>
#include <foundation/core/SmallList.h>

struct NamedBrand {};

int main() {
    foundation::core::View<int, 4> const view =
        foundation::core::View<int>{}.window<4>(0).expect("the View lends four elements");
    return foundation::core::ViewMut<int, NamedBrand, 4>{foundation::core::SmallListFrameKeyDoor::key_(), view}
        .get(0)
        .is_some();
}
