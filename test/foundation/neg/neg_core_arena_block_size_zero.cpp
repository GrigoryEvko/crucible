// A block size of zero bytes does not compile: ArenaBlockSize takes a
// constant Quantity, and its constructor refuses zero at compile time.  So
// no arena has blocks of zero bytes, and the core of an arena checks no size
// at run time.

#include <foundation/core/BumpArena.h>

#include <cstddef>

struct[[= ::foundation::brand::arena_family_brand{}]] Family final {};

int main() {
    ::foundation::core::BumpArena<Family> const arena = ::foundation::core::mint_bump_arena<Family>(
        ::foundation::core::Quantity<::foundation::core::unit::Byte, std::size_t>{0});
    return static_cast<int>(arena.retained_bytes());
}
