// Negative-compile test.  This file must fail to compile.
//
// GilHeld proves that the calling thread holds the GIL, and only
// mint_gil_held asks the interpreter for that fact.  A direct construction
// makes a proof with no question to the interpreter, so the constructor of
// GilHeld is private.

#include "../../vessel/torch/gil_held.h"

int main() {
    const crucible::vessel::GilHeld forged{};
    static_cast<void>(forged);
    return 0;
}
