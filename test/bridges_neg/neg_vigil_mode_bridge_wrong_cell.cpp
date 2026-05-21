// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-020 fixture for the CanMintVigilModeBridge concept gate.
// mint_vigil_mode_bridge is now a function template gated on
// `requires CanMintVigilModeBridge<Cell>` (VigilModeHandle.h §XXI).
// Calling with a non-ModeCell type (here a bare uint64_t standing in
// for some other AtomicMachineCell-shaped storage) must trip the
// concept's `std::same_as<std::remove_cvref_t<Cell>, ModeCell>`
// clause and be rejected with a clean concept-violation diagnostic.
//
// Expected diagnostic: "constraints not satisfied" /
// "CanMintVigilModeBridge" / "same_as" / "ModeCell" / "uint64_t".

#include <crucible/bridges/VigilModeHandle.h>

namespace vm = crucible::vigil_mode;

int main() {
    std::uint64_t fake_cell{0};
    auto h = vm::mint_vigil_mode_bridge<std::uint64_t>(fake_cell);
    (void)h;
    return 0;
}
