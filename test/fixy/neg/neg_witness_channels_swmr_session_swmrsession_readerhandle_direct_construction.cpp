// Written by `loop derive witness-fixtures --write` from the witness row of
// fixy::channels::swmr_session::SwmrSession<int, fixy::channels::swmr_session::detail::swmr_session_witness::WriterTag, fixy::channels::swmr_session::detail::swmr_session_witness::ReaderTag, fixy::channels::swmr_session::detail::swmr_session_witness::ReaderBrand, fixy::channels::swmr_session::detail::swmr_session_witness::WriterBrand>::ReaderHandle in utils/rule-table.txt.  Edit the row, not this file.
// clang-format off: the writer owns this layout, and --check compares it byte for byte.
//
// fixy::channels::swmr_session::SwmrSession<int, fixy::channels::swmr_session::detail::swmr_session_witness::WriterTag, fixy::channels::swmr_session::detail::swmr_session_witness::ReaderTag, fixy::channels::swmr_session::detail::swmr_session_witness::ReaderBrand, fixy::channels::swmr_session::detail::swmr_session_witness::WriterBrand>::ReaderHandle attests to a fact that it cannot see.  The expression below is
// the raw data that a forger gives its constructor, and the fixture keeps the door
// shut: the compiler must refuse the construction with "is private within this context".
//
// The second diagnostic is the constructor as the compiler writes it, with
// parentheses.  The source spells it with braces, so the text of the fixture
// cannot match it.

#include <fixy/channels/SwmrSession.h>

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
    [[maybe_unused]] auto forged = fixy::channels::swmr_session::SwmrSession<int, fixy::channels::swmr_session::detail::swmr_session_witness::WriterTag, fixy::channels::swmr_session::detail::swmr_session_witness::ReaderTag, fixy::channels::swmr_session::detail::swmr_session_witness::ReaderBrand, fixy::channels::swmr_session::detail::swmr_session_witness::WriterBrand>::ReaderHandle{forge::lvalue<fixy::channels::swmr_session::SwmrSession<int, fixy::channels::swmr_session::detail::swmr_session_witness::WriterTag, fixy::channels::swmr_session::detail::swmr_session_witness::ReaderTag, fixy::channels::swmr_session::detail::swmr_session_witness::ReaderBrand, fixy::channels::swmr_session::detail::swmr_session_witness::WriterBrand>>(), forge::rvalue<foundation::permissions::SharedPermissionGuard<fixy::channels::swmr_session::detail::swmr_session_witness::ReaderTag, fixy::channels::swmr_session::detail::swmr_session_witness::ReaderBrand>>()};
    return 0;
}
