// An arena has no public constructor: only its mint builds it, so each arena
// of a named tag comes from mint_bump_arena, and each scope arena from
// with_bump_arena.

#include <foundation/core/BumpArena.h>

#include <cstddef>

struct[[= ::foundation::brand::arena_family_brand{}]] Family final {};

int main() {
    ::foundation::core::BumpArena<Family> const arena{
        ::foundation::core::Quantity<::foundation::core::unit::Byte, std::size_t>{4096}};
    return static_cast<int>(arena.retained_bytes());
}
