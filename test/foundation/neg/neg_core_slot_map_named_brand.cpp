// The mint gives the brand of its own call, and a pack refuses each template
// argument after the word of the generation.  A caller that names a brand it
// holds would give two maps one brand, and a key of one would read the other.

#include <foundation/core/SlotMap.h>
#include <foundation/effects/Effect.h>

#include <cstdint>

struct HandleTag final {};
struct HeldBrand final {};

int main() {
    auto map = ::foundation::core::mint_slot_map<int, HandleTag, std::uint32_t, HeldBrand>(
        ::foundation::effects::testing::test().alloc,
        ::foundation::core::Quantity<::foundation::core::unit::Element, std::uint32_t>{4});
    return map.is_empty() ? 0 : 1;
}
