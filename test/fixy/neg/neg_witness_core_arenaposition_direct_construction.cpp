// Written by `loop derive witness-fixtures --write` from the witness row of
// foundation::core::ArenaPosition<int, forge::arena> in utils/rule-table.txt.  Edit the row, not this file.
// clang-format off: the writer owns this layout, and --check compares it byte for byte.
//
// foundation::core::ArenaPosition<int, forge::arena> attests to a fact that it cannot see.  The expression below is
// the raw data that a forger gives its constructor, and the fixture keeps the door
// shut: the compiler must refuse the construction with "is private within this context".
//
// The second diagnostic is the constructor as the compiler writes it, with
// parentheses.  The source spells it with braces, so the text of the fixture
// cannot match it.

#include <foundation/core/BumpArena.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace forge {
// Stand-ins for the values that a forger must give.  The compiler refuses the
// construction, so no fixture links, and neither stand-in has a body.
template <class T> T& lvalue() noexcept;
template <class T> T&& rvalue() noexcept;

// The family tag of arenas that the expression names.
struct [[= ::foundation::brand::arena_family_brand{}]] arena final {};
}  // namespace forge

int main() {
    [[maybe_unused]] auto forged = foundation::core::ArenaPosition<int, forge::arena>{forge::rvalue<foundation::core::Identity>(), forge::rvalue<foundation::core::Index<64>>(), forge::rvalue<foundation::core::ByteCount>()};
    return 0;
}
