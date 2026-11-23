// A function makes a local arena and gives back a Ref that the arena gave,
// through erase_brand.  The end of the arena gives its blocks back, so the
// Ref names freed bytes.  make marks the arena with CRUCIBLE_LIFETIMEBOUND,
// and the rule view-escape refuses the return.

#include <foundation/core/BumpArena.h>
#include <foundation/effects/Effect.h>

#include <cstddef>

namespace {

struct[[= ::foundation::brand::arena_family_brand{}]] Family final {};

[[nodiscard]] auto ref_of_dead_arena(::foundation::effects::Alloc alloc) noexcept {
    ::foundation::core::BumpArena<Family> arena = ::foundation::core::mint_bump_arena<Family>(
        ::foundation::core::Quantity<::foundation::core::unit::Byte, std::size_t>{4096});
    return ::foundation::core::erase_brand(arena.make<int>(alloc, 7));
}

}  // namespace

int main() { return *ref_of_dead_arena(::foundation::effects::testing::test().alloc); }
