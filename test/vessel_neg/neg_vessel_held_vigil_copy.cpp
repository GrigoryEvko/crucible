// Negative-compile test.  This file must fail to compile.
//
// A HeldVigil names one claim of one thread on the slot of a Vigil.  A copy
// could go to another thread, which holds no claim, so the proof has no copy.

#include "../../vessel/torch/vigil_handles.h"

namespace {
[[maybe_unused, nodiscard]] crucible::vessel::HeldVigil copy_of(crucible::vessel::HeldVigil const& proof) {
    return proof;
}
}  // namespace

int main() { return 0; }
