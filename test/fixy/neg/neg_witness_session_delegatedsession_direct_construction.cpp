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
#include <utility>

namespace forge {
// Stand-ins for the values that a forger must give.  The compiler refuses the
// construction, so no fixture links, and neither stand-in has a body.
template <class T> T& lvalue() noexcept;
template <class T> T&& rvalue() noexcept;
}  // namespace forge

int main() {
    [[maybe_unused]] auto forged = fixy::session::DelegatedSession<fixy::session::End, int, fixy::session::check::Enforced, foundation::permissions::EmptyPermSet>{forge::rvalue<fixy::session::SessionHandle<fixy::session::End, int, void, fixy::session::check::Enforced, foundation::permissions::EmptyPermSet>>(), forge::rvalue<fixy::session::PermHold<foundation::permissions::EmptyPermSet>>()};
    return 0;
}
