// The alignment of the storage of a FixedHeapArray is a power of two, as for a
// HeapArray.  An alignment of 12 fails the gate of the mint.

#include <foundation/core/FixedHeapArray.h>
#include <foundation/effects/Effect.h>

int main() {
    auto numbers =
        ::foundation::core::mint_fixed_heap_array<int, 4, 12>(::foundation::effects::testing::test().alloc, 1);
    return static_cast<int>(numbers.size());
}
