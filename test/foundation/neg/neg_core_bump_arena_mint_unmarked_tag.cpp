// mint_bump_arena takes a named arena tag only.  A class with no mark of an
// arena tag names no kind of arena, so the mint refuses it.

#include <foundation/core/BumpArena.h>

#include <cstddef>

struct Unmarked final {};

int main() {
    auto const arena = ::foundation::core::mint_bump_arena<Unmarked>(
        ::foundation::core::Quantity<::foundation::core::unit::Byte, std::size_t>{4096});
    return static_cast<int>(arena.retained_bytes());
}
