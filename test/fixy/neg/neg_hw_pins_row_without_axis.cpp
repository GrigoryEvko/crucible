// A row that lists no axis states nothing, even for a block that holds no
// atom alias either.  The row fails, so a block cannot be kept on the roster
// by a row that pins nothing.

#include "../hw_axis_pins.h"

namespace planted::site_hw {
inline constexpr int kPrefetchLocality = 3;
}  // namespace planted::site_hw

static_assert(hw_axis_pins::pinned<^^::planted::site_hw>);

int main() { return 0; }
