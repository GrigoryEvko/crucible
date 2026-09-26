// A row lists an axis that no atom alias of its block engages.  The block
// states its instruction class and no SIMD ISA, so the row that claims
// SimdIsa fails, and the report names the axis that the block lost.

#include "../hw_axis_pins.h"

#include <fixy/atoms/Hw.h>

namespace planted::site_hw {
using InstructionTier = ::fixy::atom::hw::scalar;
}  // namespace planted::site_hw

static_assert(hw_axis_pins::pinned<^^::planted::site_hw, ::fixy::Axis::SimdIsa, ::fixy::Axis::HwInstruction>);

int main() { return 0; }
