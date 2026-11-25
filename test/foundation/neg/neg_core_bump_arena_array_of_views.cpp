// array of a BumpArena fills a run of Views.  A View names no owner, and the
// run lives until the arena ends, so array refuses the element (objects of an
// arena of foundation/core/BumpArena.h).

#include <foundation/core/BumpArena.h>
#include <foundation/core/Region.h>
#include <foundation/effects/Effect.h>

#include <cstddef>

struct[[= ::foundation::brand::arena_family_brand{}]] Family final {};

int main() {
    ::foundation::core::BumpArena<Family> arena = ::foundation::core::mint_bump_arena<Family>(
        ::foundation::core::Quantity<::foundation::core::unit::Byte, std::size_t>{4096});
    auto const run =
        arena.array(::foundation::effects::testing::test().alloc, 2, ::foundation::core::View<int const>{});
    return static_cast<int>(run.size());
}
