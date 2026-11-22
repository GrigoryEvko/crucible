// Negative compile test.  This file must fail to compile.
//
// Each write of the mode cell takes the proof of its one writer.  A publish
// with no writer would give the cell a writer that no proof names.
//
// Expected diagnostic: no publish of the cell takes no argument.
#include <fixy/session/VigilModeCell.h>

namespace vm = fixy::session::vigil_mode;

int main() {
    const vm::ModeWriter writer = vm::mint_mode_writer();
    vm::ModeCell cell{writer};
    cell.publish_compiled();
    return cell.load() == vm::Mode::COMPILED ? 0 : 1;
}
