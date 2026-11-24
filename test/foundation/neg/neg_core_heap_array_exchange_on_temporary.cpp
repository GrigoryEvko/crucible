// An exchange in a temporary HeapArray writes storage that the full
// expression frees, so the element that it gives back names no array.  The
// rvalue form of the door is deleted.

#include <foundation/core/Array.h>
#include <foundation/effects/Effect.h>

int main() {
    int const held =
        ::foundation::core::mint_heap_array(::foundation::effects::testing::test().alloc, 4, 0).exchange(0, 1);
    return held;
}
