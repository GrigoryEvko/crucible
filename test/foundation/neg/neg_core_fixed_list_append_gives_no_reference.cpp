// A caller of append keeps a reference to the element that it put in, and a
// take of the list then ends that element under the reference.  append gives
// the index of the element, so it gives no reference to keep.

#include <foundation/core/Array.h>
#include <foundation/core/List.h>
#include <foundation/effects/Effect.h>

namespace fc = ::foundation::core;

int main() {
    auto const alloc = ::foundation::effects::testing::test().alloc;
    fc::FixedList<fc::HeapArray<int>, 2> runs{};
    fc::HeapArray<int> const& held = *runs.append(fc::mint_heap_array<int>(alloc, 4, 7)).expect("the list has room");
    static_cast<void>(runs.take_last());
    return *held.get(0).expect("the array holds an element");
}
