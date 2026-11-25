// A unit of the base that is no friend of the door of AlignedBytes.h calls its
// member, and builds a ViewMut of a brand that it names over a run of its own.
// The six frame doors of the windows are the friends of the door, so its
// member is private here.

#include <foundation/AlignedBytes.h>
#include <foundation/core/List.h>

struct NamedBrand {};

int main() {
    foundation::core::View<int, 4> const view =
        foundation::core::View<int>{}.window<4>(0).expect("the View lends four elements");
    return foundation::core::ViewMut<int, NamedBrand, 4>{foundation::core::AlignedBytesFrameKeyDoor::key_(), view}
        .get(0)
        .is_some();
}
