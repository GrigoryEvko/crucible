// Written by `loop derive witness-fixtures --write` from the witness row of
// foundation::effects::host::ProducerClaim<forge::brand> in utils/rule-table.txt.  Edit the row, not this file.
// clang-format off: the writer owns this layout, and --check compares it byte for byte.
//
// foundation::effects::host::ProducerClaim<forge::brand> attests to a fact that it cannot see.  The expression below is
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

// The brand that the expression names.  It is empty, which is all that
// foundation::brand::IsBrand asks of a brand.
struct brand {};
}  // namespace forge

int main() {
    [[maybe_unused]] auto forged = foundation::effects::host::ProducerClaim<forge::brand>{};
    return 0;
}
