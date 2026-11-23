// mint_member_slot_map takes a member tag only (ArenaMemberTag of
// foundation/core/ArenaTag.h).  A class with no mark of a member tag names
// no member, so the rule brand-escape could not count the maps of its brand,
// and the mint refuses it.

#include <foundation/core/SlotMap.h>

#include <foundation/effects/Effect.h>

#include <cstdint>

struct HandleTag final {};
struct Unmarked final {};

int main() {
    auto const map = ::foundation::core::mint_member_slot_map<int, HandleTag, Unmarked>(
        ::foundation::effects::testing::test().alloc,
        ::foundation::core::Quantity<::foundation::core::unit::Element, std::uint32_t>{4});
    return static_cast<int>(map.size());
}
