// remove moves a value out of its slot, so a map refuses a value whose move
// can throw: a throw would leave the slot with no value and a live generation.

#include <foundation/core/SlotMap.h>
#include <foundation/effects/Effect.h>

#include <cstdint>

struct HandleTag final {};

struct ThrowingMove final {
    int value = 0;
    ThrowingMove(ThrowingMove&&) noexcept(false) {}
};

int main() {
    auto const handles = ::foundation::core::mint_slot_map<ThrowingMove, HandleTag>(
        ::foundation::effects::testing::test().alloc,
        ::foundation::core::Quantity<::foundation::core::unit::Element, std::uint32_t>{4});
    return static_cast<int>(handles.size());
}
