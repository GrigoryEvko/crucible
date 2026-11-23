// A unit of the base that is no friend of the door of WindowStack.h calls its
// member, and builds a ViewMut of a brand that it names over a run of its own.
// WindowLoan is the one friend of the door, so its member is private here.

#include <foundation/core/List.h>
#include <foundation/core/WindowStack.h>

struct NamedBrand {};

int main() {
    foundation::core::FixedList<int, 4> list{};
    foundation::core::View<int, 4> const view = list.view().window<4>(0).expect("the list lends four slots");
    return foundation::core::ViewMut<int, NamedBrand, 4>{foundation::core::WindowLoanFrameKeyDoor::key_(), view}
        .get(0)
        .is_some();
}
