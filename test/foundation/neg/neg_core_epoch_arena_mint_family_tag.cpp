// mint_epoch_arena takes a member tag only.  The arenas of a family tag share
// the brand, so an epoch of one could not tell its bytes from the bytes of a
// different arena of the family, and the mint refuses the tag.

#include <foundation/core/BumpArena.h>

#include <cstddef>

struct[[= ::foundation::brand::arena_family_brand{}]] Family final {};

int main() {
    auto const arena = ::foundation::core::mint_epoch_arena<Family>(
        ::foundation::core::Quantity<::foundation::core::unit::Byte, std::size_t>{4096});
    return static_cast<int>(arena.retained_bytes());
}
