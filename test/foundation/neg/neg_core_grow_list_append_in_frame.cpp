// An arm of the frame door with_element appends to its GrowList.  The append
// can move the storage that the arm borrows, so the element would dangle.
// The door carries the annotation borrow_match, and the rule borrow-match
// refuses a call of a member of the list that is not const in the arm.

#include <foundation/core/Grow.h>
#include <foundation/effects/Effect.h>

int main() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    ::foundation::core::GrowList<int> list = ::foundation::core::mint_grow_list<int>(64);
    list.append(alloc, 1).expect("the list is below its maximum");
    return list.with_element(
        0,
        [&](int& first) noexcept {
            static_cast<void>(list.append(alloc, 2));
            return first;
        },
        [] noexcept { return 0; });
}
