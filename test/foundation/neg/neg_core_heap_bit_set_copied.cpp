// A copy of a HeapBitSet would allocate with no allocation capability at its
// call, so a set from mint_heap_bit_set does not copy.

#include <foundation/core/BitSet.h>
#include <foundation/effects/Effect.h>

#include <cstdint>

struct SlotTag final {};

int main() {
    ::foundation::core::HeapBitSet<SlotTag> const slots = ::foundation::core::mint_heap_bit_set<SlotTag>(
        ::foundation::effects::testing::test().alloc,
        ::foundation::core::Quantity<::foundation::core::unit::Element, std::uint32_t>{8});
    ::foundation::core::HeapBitSet<SlotTag> const copy = slots;
    return static_cast<int>(copy.size());
}
