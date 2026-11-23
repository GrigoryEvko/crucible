// A SlotMap takes a key and no number.  A caller states a word from outside as
// a key with the key_type of the map, so the claim shows at the call.

#include <foundation/core/SlotMap.h>
#include <foundation/effects/Effect.h>

#include <cstdint>

struct HandleTag final {};

int main() {
    auto handles = ::foundation::core::mint_slot_map<int, HandleTag>(
        ::foundation::effects::testing::test().alloc,
        ::foundation::core::Quantity<::foundation::core::unit::Element, std::uint32_t>{4});
    return handles.get(std::uint64_t{1}).is_some() ? 1 : 0;
}
