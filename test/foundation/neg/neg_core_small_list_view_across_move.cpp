// A View of the inline elements of a SmallList would point into the object of
// the list, and a move of the list copies those elements to a different
// object.  A SmallList gives no View out of a frame door, so a View across a
// move does not compile.

#include <foundation/core/SmallList.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <utility>

int main() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    auto list = ::foundation::core::mint_small_list<int, 4>(
        ::foundation::core::Quantity<::foundation::core::unit::Element, std::size_t>{64});
    list.append(alloc, 1).expect("the list is below its maximum");
    auto const view = list.view();
    auto const moved = std::move(list);
    return static_cast<int>(view.size() + moved.size());
}
