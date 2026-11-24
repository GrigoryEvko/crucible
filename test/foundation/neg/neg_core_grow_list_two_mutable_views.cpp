// A body of the frame door with_view writes the elements of a GrowList
// through the View that the door gives.  Two copies of a View that writes
// would name the same elements, so no proof that two runs do not overlap
// would hold for them.  The door with_view gives a View of the const
// elements, so no body gets a View that writes.  A body that writes takes
// with_mut, whose ViewMut does not copy.  The body keeps no copy of the View,
// because the store check of the door refuses a body that does.

#include <foundation/core/Grow.h>
#include <foundation/effects/Effect.h>

int main() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    ::foundation::core::GrowList<int> list = ::foundation::core::mint_grow_list<int>(64);
    list.append(alloc, 1).expect("the list is below its maximum");
    return list.with_view([](auto view) noexcept -> int {
        for (int& element : view) {
            element = 2;
        }
        return 0;
    });
}
