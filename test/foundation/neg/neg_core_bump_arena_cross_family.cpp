// A Ref of an arena carries the tag of the arena, and no conversion changes
// a brand.  So a Ref of an arena of one family does not fit a Ref of a
// different family.

#include <foundation/core/BumpArena.h>
#include <foundation/effects/Effect.h>

#include <cstddef>

struct[[= ::foundation::brand::arena_family_brand{}]] Family final {};
struct[[= ::foundation::brand::arena_family_brand{}]] Other final {};

int main() {
    ::foundation::core::BumpArena<Family> arena = ::foundation::core::mint_bump_arena<Family>(
        ::foundation::core::Quantity<::foundation::core::unit::Byte, std::size_t>{4096});
    ::foundation::core::Ref<int, Other> const wrong = arena.make<int>(::foundation::effects::testing::test().alloc, 5);
    return *wrong;
}
