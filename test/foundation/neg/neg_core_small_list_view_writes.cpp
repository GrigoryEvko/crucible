// The body of the frame door with_view of a SmallList takes a View that
// writes the elements.  A View copies, so a body could hold two Views that
// write the same elements, beside the ViewMut of a frame of the list.  The
// door lends only the View of the const elements, and a body that writes
// takes with_mut, so no overload takes this body.

#include <foundation/core/SmallList.h>
#include <foundation/effects/Effect.h>

#include <cstddef>

int main() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    auto list = ::foundation::core::mint_small_list<int, 2>(
        ::foundation::core::Quantity<::foundation::core::unit::Element, std::size_t>{64});
    list.append(alloc, 1).expect("the list is below its maximum");
    list.with_view([](::foundation::core::View<int> view) noexcept {
        ::foundation::core::View<int> const twin = view;
        for (int& element : twin) {
            element = 2;
        }
    });
    return 0;
}
