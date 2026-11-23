// Two maps of one domain, from two calls of the mint.  The key of the first
// names the slot of the second with an equal generation, so a read through it
// would give a value of the second map.  Each call of the mint gives a brand
// of its own, and the key carries it, so the second map refuses the key by its
// type.

#include <foundation/core/SlotMap.h>
#include <foundation/effects/Effect.h>

#include <cstdint>

struct HandleTag final {};

int main() {
    auto first = ::foundation::core::mint_slot_map<int, HandleTag>(
        ::foundation::effects::testing::test().alloc,
        ::foundation::core::Quantity<::foundation::core::unit::Element, std::uint32_t>{4});
    auto second = ::foundation::core::mint_slot_map<int, HandleTag>(
        ::foundation::effects::testing::test().alloc,
        ::foundation::core::Quantity<::foundation::core::unit::Element, std::uint32_t>{4});
    auto const key = first.insert(1).expect("the map has a free slot");
    static_cast<void>(second.insert(2).expect("the map has a free slot"));
    return second.remove(key).is_some() ? 1 : 0;
}
