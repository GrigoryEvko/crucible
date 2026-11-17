// A Ref of an element of a SmallList would name a slot that the append past
// the inline slots leaves: the append moves each element to the heap, and the
// read through the Ref would see a dead slot.  A SmallList gives no Ref out of
// a frame door, so the read of an element across an append does not compile.

#include <foundation/core/SmallList.h>
#include <foundation/effects/Effect.h>

#include <cstddef>

int main() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    auto list = ::foundation::core::mint_small_list<int, 2>(
        ::foundation::core::Quantity<::foundation::core::unit::Element, std::size_t>{64});
    list.append(alloc, 1).expect("the list is below its maximum");
    auto const first = list.get(0);
    for (int value = 2; value <= 8; ++value) {
        list.append(alloc, value).expect("the list is below its maximum");
    }
    return first.is_some() ? 0 : 1;
}
