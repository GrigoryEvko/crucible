// A SlotMap takes the key of its own domain.  A key of a different map can
// name a slot of this map with an equal generation, so the map refuses it by
// its type.

#include <foundation/core/SlotMap.h>
#include <foundation/effects/Effect.h>

#include <cstdint>

struct HandleTag final {};
struct OtherTag final {};

int main() {
    auto handles = ::foundation::core::mint_slot_map<int, HandleTag>(
        ::foundation::effects::testing::test().alloc,
        ::foundation::core::Quantity<::foundation::core::unit::Element, std::uint32_t>{4});
    auto others = ::foundation::core::mint_slot_map<int, OtherTag>(
        ::foundation::effects::testing::test().alloc,
        ::foundation::core::Quantity<::foundation::core::unit::Element, std::uint32_t>{4});
    ::foundation::core::SlotKey<OtherTag> const key = others.insert(1).expect("the map has a free slot");
    return handles.get(key).is_some() ? 1 : 0;
}
