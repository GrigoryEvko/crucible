// mint_bump_arena takes a named arena tag of one kind.  A class with the mark
// of a member tag and the mark of a family tag is neither, so the mint
// refuses it.

#include <foundation/core/BumpArena.h>

#include <cstddef>

struct[[= ::foundation::brand::arena_member_brand{}]][[= ::foundation::brand::arena_family_brand{}]] Both final {};

int main() {
    auto const arena = ::foundation::core::mint_bump_arena<Both>(
        ::foundation::core::Quantity<::foundation::core::unit::Byte, std::size_t>{4096});
    return static_cast<int>(arena.retained_bytes());
}
