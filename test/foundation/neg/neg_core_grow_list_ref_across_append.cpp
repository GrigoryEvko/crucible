// A Ref of get() named an element in the storage of a GrowList.  An append
// that grows the list moves each element and frees the old storage, so the
// Ref dangled, and a read through it was a heap-use-after-free.  A GrowList
// gives no Ref out of a frame door, so the read of an element across an
// append does not compile.

#include <foundation/core/Grow.h>
#include <foundation/effects/Effect.h>

int main() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    ::foundation::core::GrowList<int> list = ::foundation::core::mint_grow_list<int>(1024);
    list.append(alloc, 1).expect("the list is below its maximum");
    auto const first = list.get(0);
    for (int value = 2; value <= 1024; ++value) {
        list.append(alloc, value).expect("the list is below its maximum");
    }
    return first.is_some() ? 0 : 1;
}
