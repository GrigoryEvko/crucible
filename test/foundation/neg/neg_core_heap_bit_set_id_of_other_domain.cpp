// A HeapBitSet takes the id of its own domain.  The id of an op is no id of a
// slot, so a set of slots refuses it.

#include <foundation/core/BitSet.h>
#include <foundation/effects/Effect.h>

#include <cstdint>

struct SlotTag final {};
struct OpTag final {};

int main() {
    ::foundation::core::HeapBitSet<SlotTag> slots = ::foundation::core::mint_heap_bit_set<SlotTag>(
        ::foundation::effects::testing::test().alloc,
        ::foundation::core::Quantity<::foundation::core::unit::Element, std::uint32_t>{8});
    slots.set(::foundation::core::BitId<OpTag>{3});
    return static_cast<int>(slots.size());
}
