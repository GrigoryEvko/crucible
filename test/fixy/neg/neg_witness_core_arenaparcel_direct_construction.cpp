// Written by `loop derive witness-fixtures --write` from the witness row of
// foundation::core::ArenaParcel<forge::arena, int> in utils/rule-table.txt.  Edit the row, not this file.
// clang-format off: the writer owns this layout, and --check compares it byte for byte.
//
// foundation::core::ArenaParcel<forge::arena, int> attests to a fact that it cannot see.  The expression below is
// the raw data that a forger gives its constructor, and the fixture keeps the door
// shut: the compiler must refuse the construction with "is private within this context".
//
// The second diagnostic is the constructor as the compiler writes it, with
// parentheses.  The source spells it with braces, so the text of the fixture
// cannot match it.

#include <foundation/core/ArenaFrame.h>

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
    [[maybe_unused]] auto forged = foundation::core::ArenaParcel<forge::arena, int>{forge::rvalue<foundation::core::Box<foundation::core::BumpArena<forge::arena>>>(), forge::lvalue<foundation::core::Record<foundation::core::ArenaRun<int, forge::arena>>>()};
    return 0;
}
