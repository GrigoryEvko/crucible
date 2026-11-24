// A caller builds an ArenaPosition from parts that it chose.  The position
// would name bytes that no make of the arena gave, so at() would give a Ref
// to an object that does not exist.  The constructor is private, and only
// position_of of the arena that holds the object builds a position.

#include <foundation/core/BumpArena.h>
#include <foundation/core/Identity.h>
#include <foundation/core/Index.h>
#include <foundation/core/Quantity.h>

#include <cstddef>

struct[[= ::foundation::brand::arena_family_brand{}]] Family final {};

int main() {
    ::foundation::core::ArenaPosition<int, Family> const position{
        ::foundation::core::mint_identity(), ::foundation::core::index<0>(),
        ::foundation::core::Quantity<::foundation::core::unit::Byte, std::size_t>{16}};
    return position == position ? 0 : 1;
}
