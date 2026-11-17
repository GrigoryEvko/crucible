// The generation of a slot fits in the high 32 bits of its key, so a map
// refuses a word of a generation of 64 bits.

#include <foundation/core/SlotMap.h>
#include <foundation/effects/Effect.h>

#include <cstdint>

struct HandleTag final {};

int main() {
    auto const handles = ::foundation::core::mint_slot_map<int, HandleTag, std::uint64_t>(
        ::foundation::effects::testing::test().alloc,
        ::foundation::core::Quantity<::foundation::core::unit::Element, std::uint32_t>{4});
    return static_cast<int>(handles.size());
}
