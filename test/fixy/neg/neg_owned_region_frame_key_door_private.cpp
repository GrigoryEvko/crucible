// A unit of the base that is no friend of the door of fixy/OwnedRegion.h
// calls its member, and builds a ViewMut of a brand that it names over a run
// of its own.  The frame door of a region and the region scope door over a
// HeapArray are the friends of the door, so its member is private here.

#include <fixy/OwnedRegion.h>
#include <foundation/core/List.h>

struct NamedBrand {};

int main() {
    foundation::core::View<int, 4> const view =
        foundation::core::View<int>{}.window<4>(0).expect("the View lends four elements");
    return foundation::core::ViewMut<int, NamedBrand, 4>{foundation::core::OwnedRegionFrameKeyDoor::key_(), view}
        .get(0)
        .is_some();
}
