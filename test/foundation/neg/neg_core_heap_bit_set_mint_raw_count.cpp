// The mint of a HeapBitSet takes its count as a Quantity of elements, so a
// plain number does not state its unit at the call.

#include <foundation/core/BitSet.h>
#include <foundation/effects/Effect.h>

#include <cstdint>

struct SlotTag final {};

int main() {
    ::foundation::core::HeapBitSet<SlotTag> const slots =
        ::foundation::core::mint_heap_bit_set<SlotTag>(::foundation::effects::testing::test().alloc, std::uint32_t{8});
    return static_cast<int>(slots.size());
}
