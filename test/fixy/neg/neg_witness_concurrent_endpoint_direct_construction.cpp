// Written by `loop derive witness-fixtures --write` from the witness row of
// fixy::concurrent::Endpoint<fixy::concurrent::PermissionedSpscChannel<int, 64, fixy::concurrent::detail::endpoint_witness::UserTag, fixy::concurrent::detail::endpoint_witness::UserBrand>, fixy::concurrent::Direction::Producer, fixy::concurrent::detail::endpoint_witness::FgCtx> in utils/rule-table.txt.  Edit the row, not this file.
// clang-format off: the writer owns this layout, and --check compares it byte for byte.
//
// fixy::concurrent::Endpoint<fixy::concurrent::PermissionedSpscChannel<int, 64, fixy::concurrent::detail::endpoint_witness::UserTag, fixy::concurrent::detail::endpoint_witness::UserBrand>, fixy::concurrent::Direction::Producer, fixy::concurrent::detail::endpoint_witness::FgCtx> attests to a fact that it cannot see.  The expression below is
// the raw data that a forger gives its constructor, and the fixture keeps the door
// shut: the compiler must refuse the construction with "is private within this context".
//
// The second diagnostic is the constructor as the compiler writes it, with
// parentheses.  The source spells it with braces, so the text of the fixture
// cannot match it.

#include <fixy/concurrent/Endpoint.h>

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
    [[maybe_unused]] auto forged = fixy::concurrent::Endpoint<fixy::concurrent::PermissionedSpscChannel<int, 64, fixy::concurrent::detail::endpoint_witness::UserTag, fixy::concurrent::detail::endpoint_witness::UserBrand>, fixy::concurrent::Direction::Producer, fixy::concurrent::detail::endpoint_witness::FgCtx>{forge::lvalue<fixy::concurrent::detail::endpoint_witness::FgCtx>(), forge::rvalue<fixy::concurrent::PermissionedSpscChannel<int, 64, fixy::concurrent::detail::endpoint_witness::UserTag, fixy::concurrent::detail::endpoint_witness::UserBrand>::ProducerHandle>()};
    return 0;
}
