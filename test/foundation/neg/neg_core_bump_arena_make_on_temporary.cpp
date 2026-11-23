// An object in a temporary arena dangles at the end of the statement, because
// the arena gives back its blocks there.  The rvalue form of make is deleted.

#include <foundation/core/BumpArena.h>
#include <foundation/effects/Effect.h>

#include <cstddef>

struct[[= ::foundation::brand::arena_family_brand{}]] Family final {};

int main() {
    auto const object = ::foundation::core::mint_bump_arena<Family>(
                            ::foundation::core::Quantity<::foundation::core::unit::Byte, std::size_t>{4096})
                            .make<int>(::foundation::effects::testing::test().alloc, 5);
    return *object;
}
