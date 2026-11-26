// Negative-compile test.  This file must fail to compile.
//
// Violation: an OpNest built with no call of the nest door of its stack.
//
// The constructor of OpNest is private, and only the nest door of the stack
// and the nest of the Vigil call it, each for a local of a door.  So the
// nests of one stack close in the reverse order of their opens.  A nest that
// a caller builds itself could live in an object that ends out of that order,
// and its close would give back a depth that another nest still holds.

#include <crucible/ValidatedOp.h>

int main() {
    crucible::OpRecordStack stack{};
    const crucible::OpNest nest{stack};
    return static_cast<int>(nest.depth().raw());
}
