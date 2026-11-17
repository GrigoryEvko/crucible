// The mint of a SlotMap takes the allocation capability first, so each
// allocation shows at its call.  A different capability tag fails the gate.

#include <foundation/core/SlotMap.h>
#include <foundation/effects/Effect.h>

#include <cstdint>

struct HandleTag final {};

int main() {
    auto const handles = ::foundation::core::mint_slot_map<int, HandleTag>(
        ::foundation::effects::testing::test().io,
        ::foundation::core::Quantity<::foundation::core::unit::Element, std::uint32_t>{4});
    return static_cast<int>(handles.size());
}
