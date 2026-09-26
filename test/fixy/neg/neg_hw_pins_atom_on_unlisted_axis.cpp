// A block holds an atom alias of an axis that its row does not list.  The
// block states a fence strength, and the row claims only the instruction
// class, so the row fails and the report names the unlisted axis.  A type
// alias that is not an atom is no claim, so the lattice alias beside it is
// not reported.

#include "../hw_axis_pins.h"

#include <fixy/atoms/Barrier.h>
#include <fixy/atoms/Hw.h>
#include <foundation/algebra/lattices/BarrierStrengthLattice.h>

namespace planted::site_hw {
using InstructionTier = ::fixy::atom::hw::scalar;
using BarrierTier = ::fixy::atom::barrier::seq_cst;
using Lattice = ::foundation::algebra::lattices::BarrierStrengthLattice;
}  // namespace planted::site_hw

static_assert(hw_axis_pins::pinned<^^::planted::site_hw, ::fixy::Axis::HwInstruction>);

int main() { return 0; }
