// Negative-compile test.  This file must fail to compile.
//
// Violation: a VigilOpNest built with no call of Vigil::with_op_nest.
//
// The constructor of VigilOpNest is private, and only the nest door of the
// Vigil calls it, for a local of the door.  So the nests of the Vigil close in
// the reverse order of their opens, and each close gives its closing entry at
// the depth of its own op.

#include <crucible/Vigil.h>

int main() {
    crucible::Vigil vigil;
    const crucible::VigilFgCtx fg = vigil.mint_producer_context();
    const crucible::VigilOpNest nest{vigil, fg};
    return static_cast<int>(nest.depth().raw());
}
