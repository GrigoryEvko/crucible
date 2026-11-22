// A FixedHeapArray holds its storage from its mint to its end, so no door
// tests a null pointer.  A move would leave the source with no storage, so
// the move is refused, and a holder takes the array from the mint.

#include <foundation/core/FixedHeapArray.h>
#include <foundation/effects/Effect.h>

#include <utility>

int main() {
    ::foundation::core::FixedHeapArray<int, 4> array =
        ::foundation::core::mint_fixed_heap_array<int, 4>(::foundation::effects::testing::test().alloc, 1);
    ::foundation::core::FixedHeapArray<int, 4> other{std::move(array)};
    return other.at(::foundation::core::index<0>());
}
