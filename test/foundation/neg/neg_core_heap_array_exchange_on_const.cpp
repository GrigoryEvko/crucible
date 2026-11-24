// An exchange writes an element, and a const HeapArray has no writer of its
// elements.  The const form of the door is deleted.

#include <foundation/core/Array.h>
#include <foundation/effects/Effect.h>

int main() {
    ::foundation::core::HeapArray<int> const array =
        ::foundation::core::mint_heap_array(::foundation::effects::testing::test().alloc, 4, 0);
    int const held = array.exchange(0, 1);
    return held;
}
