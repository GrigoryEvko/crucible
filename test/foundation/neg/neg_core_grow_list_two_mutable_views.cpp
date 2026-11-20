// A body of the frame door with_view copies the View of a GrowList into two
// variables of the caller, and the caller writes through both.  The two Views
// would name the same elements, so no proof that two runs do not overlap
// would hold for them.  The door with_view gives a View of the const
// elements, so no body gets a View that writes.  A body that writes takes
// with_mut, whose ViewMut does not copy.

#include <foundation/core/Grow.h>
#include <foundation/effects/Effect.h>

int main() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    ::foundation::core::GrowList<int> list = ::foundation::core::mint_grow_list<int>(64);
    list.append(alloc, 1).expect("the list is below its maximum");
    ::foundation::core::View<int> first{};
    ::foundation::core::View<int> second{};
    static_cast<void>(list.with_view([&first, &second](auto view) noexcept -> int {
        first = second = view;
        return 0;
    }));
    for (int& element : first) {
        element = 2;
    }
    for (int& element : second) {
        element = 3;
    }
    return 0;
}
