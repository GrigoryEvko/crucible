// A block size that the program computes at run time does not convert to
// ArenaBlockSize: the constructor takes only a constant, so it checks the
// size at compile time.  A size that a caller adds up at run time starts from
// a constant above zero and grows with plus.

#include <foundation/core/BumpArena.h>

#include <cstddef>

struct[[= ::foundation::brand::arena_family_brand{}]] Family final {};

int main() {
    std::size_t counted = 64;
    counted += 64;
    ::foundation::core::BumpArena<Family> const arena = ::foundation::core::mint_bump_arena<Family>(
        ::foundation::core::Quantity<::foundation::core::unit::Byte, std::size_t>{counted});
    return static_cast<int>(arena.retained_bytes());
}
