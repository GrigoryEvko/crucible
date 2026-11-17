// A copy of a SlotMap would allocate with no allocation capability at its
// call, so a map from mint_slot_map does not copy.

#include <foundation/core/SlotMap.h>
#include <foundation/effects/Effect.h>

#include <cstdint>

struct HandleTag final {};

int main() {
    auto const handles = ::foundation::core::mint_slot_map<int, HandleTag>(
        ::foundation::effects::testing::test().alloc,
        ::foundation::core::Quantity<::foundation::core::unit::Element, std::uint32_t>{4});
    auto const copy = handles;
    return static_cast<int>(copy.size());
}
