// mint_member_slot_map takes a member tag only (ArenaMemberTag of
// foundation/core/ArenaTag.h).  A family tag names a family of owners that no
// rule counts, so two maps of the family would share the brand, and a key of
// one would name a slot of the other.  The mint refuses it.

#include <foundation/core/SlotMap.h>

#include <foundation/effects/Effect.h>

#include <cstdint>

struct HandleTag final {};
struct[[= ::foundation::brand::arena_family_brand{}]] Family final {};

int main() {
    auto const map = ::foundation::core::mint_member_slot_map<int, HandleTag, Family>(
        ::foundation::effects::testing::test().alloc,
        ::foundation::core::Quantity<::foundation::core::unit::Element, std::uint32_t>{4});
    return static_cast<int>(map.size());
}
