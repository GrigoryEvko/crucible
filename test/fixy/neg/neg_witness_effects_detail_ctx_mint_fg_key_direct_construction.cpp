// Written by `loop derive witness-fixtures --write` from the witness row of
// foundation::effects::detail::ctx_mint::fg_key in utils/rule-table.txt.  Edit the row, not this file.
// clang-format off: the writer owns this layout, and --check compares it byte for byte.
//
// foundation::effects::detail::ctx_mint::fg_key attests to a fact that it cannot see.  The expression below is
// the raw data that a forger gives its constructor, and the fixture keeps the door
// shut: the compiler must refuse the construction with "is private within this context".
//
// The second diagnostic is the constructor as the compiler writes it, with
// parentheses.  The source spells it with braces, so the text of the fixture
// cannot match it.

#include <foundation/effects/Ctx.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <utility>

namespace forge {
// Stand-ins for the values that a forger must give.  Neither returns: the
// fixture compiles, and it never runs.
template <class T> [[gnu::noinline]] T& lvalue() noexcept { std::abort(); }
template <class T> [[gnu::noinline]] T&& rvalue() noexcept { std::abort(); }
}  // namespace forge

int main() {
    [[maybe_unused]] auto forged = foundation::effects::detail::ctx_mint::fg_key{};
    return 0;
}
