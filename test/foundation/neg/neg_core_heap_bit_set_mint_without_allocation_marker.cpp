// The mint of a HeapBitSet takes the allocation capability first, so each
// allocation shows at its call.  A different capability tag fails the gate.

#include <foundation/core/BitSet.h>
#include <foundation/effects/Effect.h>

#include <cstdint>

struct SlotTag final {};

int main() {
    ::foundation::core::HeapBitSet<SlotTag> const slots = ::foundation::core::mint_heap_bit_set<SlotTag>(
        ::foundation::effects::testing::test().io,
        ::foundation::core::Quantity<::foundation::core::unit::Element, std::uint32_t>{8});
    return static_cast<int>(slots.size());
}
