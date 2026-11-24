// The object of a position in a temporary arena dangles at the end of the
// statement, because the arena gives back its blocks there.  The rvalue form
// of at() is deleted.

#include <foundation/core/BumpArena.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Ref.h>
#include <foundation/effects/Effect.h>

#include <cstddef>

struct[[= ::foundation::brand::arena_family_brand{}]] Family final {};

int main() {
    using Bytes = ::foundation::core::Quantity<::foundation::core::unit::Byte, std::size_t>;
    auto arena = ::foundation::core::mint_bump_arena<Family>(Bytes{4096});
    ::foundation::core::Ref<int, Family> const object =
        arena.make<int>(::foundation::effects::testing::test().alloc, 5);
    ::foundation::core::ArenaPosition<int, Family> const position =
        arena.position_of(object).expect("the arena made the object");
    auto const found = ::foundation::core::mint_bump_arena<Family>(Bytes{4096}).at(position);
    return found.is_some() ? 0 : 1;
}
