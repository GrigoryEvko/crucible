// A FixedHeapArray holds its storage from its mint to its end, so no door
// tests a null pointer.  An assignment would free the storage of the target
// while a View of the target can still name it, so the assignment is refused,
// as the move is (neg_core_fixed_heap_array_moved).

#include <foundation/core/FixedHeapArray.h>
#include <foundation/effects/Effect.h>

int main() {
    ::foundation::core::FixedHeapArray<int, 4> array =
        ::foundation::core::mint_fixed_heap_array<int, 4>(::foundation::effects::testing::test().alloc, 1);
    array = ::foundation::core::mint_fixed_heap_array<int, 4>(::foundation::effects::testing::test().alloc, 2);
    return array.at(::foundation::core::index<0>());
}
