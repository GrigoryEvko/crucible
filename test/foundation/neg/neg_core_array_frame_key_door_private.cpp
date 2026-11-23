// A unit of the base that is no friend of the door of Array.h calls its
// member, and builds a ViewMut of a brand that it names over a run of its own.
// FixedArray, mint_fixed_array and the free frame door of a heap array are the
// friends of the door, so its member is private here.

#include <foundation/core/Array.h>
#include <foundation/core/List.h>

struct NamedBrand {};

int main() {
    foundation::core::FixedList<int, 4> list{};
    foundation::core::View<int, 4> const view = list.view().window<4>(0).expect("the list lends four slots");
    return foundation::core::ViewMut<int, NamedBrand, 4>{foundation::core::ArrayFrameKeyDoor::key_(), view}
        .get(0)
        .is_some();
}
