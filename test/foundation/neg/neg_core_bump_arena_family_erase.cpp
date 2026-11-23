// erase_brand refuses the brand of an arena (foundation/core/Ref.h).  A Ref of
// an arena keeps the brand of its arena, so a field that stores it names the
// arena that holds its object, and no erased Ref of an arena fits a field of
// a different store.

#include <foundation/core/BumpArena.h>
#include <foundation/core/Ref.h>
#include <foundation/effects/Effect.h>

#include <cstddef>

namespace {

struct[[= ::foundation::brand::arena_family_brand{}]] Family final {};

}  // namespace

int main() {
    ::foundation::core::BumpArena<Family> arena = ::foundation::core::mint_bump_arena<Family>(
        ::foundation::core::Quantity<::foundation::core::unit::Byte, std::size_t>{4096});
    const auto erased =
        ::foundation::core::erase_brand(arena.make<int>(::foundation::effects::testing::test().alloc, 7));
    return *erased;
}
