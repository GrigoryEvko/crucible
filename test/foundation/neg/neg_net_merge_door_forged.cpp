// allow_merge is the only way to an open merge door, so a search for it
// finds each merge kind.  A door that a caller opens by hand does not
// compile: the reason of a MergeDoor is private.

#include <foundation/algebra/net/Net.h>

namespace {

using foundation::algebra::net::MergeDoor;
using foundation::algebra::net::NetName;

[[nodiscard]] consteval MergeDoor open_by_hand() noexcept {
    MergeDoor door{};
    door.reason_ = NetName{"the order of the producers"};
    return door;
}

inline constexpr MergeDoor forged = open_by_hand();

}  // namespace

int main() { return forged.is_open() ? 0 : 1; }
