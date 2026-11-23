// Written by `loop derive witness-fixtures --write` from the witness row of
// fixy::concurrent::PermissionedBoxChannel<fixy::concurrent::ChannelForm::wire, int, 8, fixy::concurrent::detail::channel_witness::WitnessTag, fixy::concurrent::detail::channel_witness::WitnessBrand>::ConsumerEnd in utils/rule-table.txt.  Edit the row, not this file.
// clang-format off: the writer owns this layout, and --check compares it byte for byte.
//
// fixy::concurrent::PermissionedBoxChannel<fixy::concurrent::ChannelForm::wire, int, 8, fixy::concurrent::detail::channel_witness::WitnessTag, fixy::concurrent::detail::channel_witness::WitnessBrand>::ConsumerEnd attests to a fact that it cannot see.  The expression below is
// the raw data that a forger gives its constructor, and the fixture keeps the door
// shut: the compiler must refuse the construction with "is private within this context".
//
// The second diagnostic is the constructor as the compiler writes it, with
// parentheses.  The source spells it with braces, so the text of the fixture
// cannot match it.

#include <fixy/concurrent/PermissionedBoxChannel.h>

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
    [[maybe_unused]] auto forged = fixy::concurrent::PermissionedBoxChannel<fixy::concurrent::ChannelForm::wire, int, 8, fixy::concurrent::detail::channel_witness::WitnessTag, fixy::concurrent::detail::channel_witness::WitnessBrand>::ConsumerEnd{forge::rvalue<fixy::concurrent::PermissionedChannel<fixy::concurrent::ChannelForm::wire, foundation::core::BoxWord<int, fixy::concurrent::PermissionedBoxChannel<fixy::concurrent::ChannelForm::wire, int, 8, fixy::concurrent::detail::channel_witness::WitnessTag, fixy::concurrent::detail::channel_witness::WitnessBrand>>, 8, fixy::concurrent::detail::channel_witness::WitnessTag, fixy::concurrent::detail::channel_witness::WitnessBrand>::ConsumerEnd>()};
    return 0;
}
