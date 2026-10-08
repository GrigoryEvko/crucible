// Written by `loop derive witness-fixtures --write` from the witness row of
// fixy::session::DelegatedSession<fixy::session::End, int, fixy::session::check::Enforced, foundation::permissions::EmptyPermSet> in utils/rule-table.txt.  Edit the row, not this file.
// clang-format off: the writer owns this layout, and --check compares it byte for byte.
//
// fixy::session::DelegatedSession<fixy::session::End, int, fixy::session::check::Enforced, foundation::permissions::EmptyPermSet> attests to a fact that it cannot see.  The expression below is
// the raw data that a forger gives its constructor, and the fixture keeps the door
// shut: the compiler must refuse the construction with "no matching function for call".
//
// The second diagnostic is the constructor as the compiler writes it, with
// parentheses.  The source spells it with braces, so the text of the fixture
// cannot match it.

#include <fixy/session/Delegate.h>

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
    [[maybe_unused]] auto forged = fixy::session::DelegatedSession<fixy::session::End, int, fixy::session::check::Enforced, foundation::permissions::EmptyPermSet>{forge::rvalue<fixy::session::SessionHandle<fixy::session::End, int, void, fixy::session::check::Enforced, foundation::permissions::EmptyPermSet>>(), forge::rvalue<fixy::session::PermHold<foundation::permissions::EmptyPermSet>>()};
    return 0;
}
