// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The mode cell has one writer at each time.  The writer is linear: a copy
// would give the cell a second writer, so the owner could write while the
// session holds the lent writer.
//
// Expected diagnostic: the copy of the writer is a deleted function.
#include <fixy/session/VigilModeCell.h>

namespace vm = fixy::session::vigil_mode;

int main() {
    vm::ModeWriter writer = vm::mint_mode_writer();
    vm::ModeWriter second = writer;
    vm::ModeCell cell{second};
    return cell.load() == vm::Mode::RECORDING ? 0 : 1;
}
