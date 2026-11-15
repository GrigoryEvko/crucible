// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A CallRef calls its callable with the arguments of its signature.  A
// callable that takes a different argument does not fit, so the constructor
// refuses it and names its gate.
//
// Expected diagnostic: no constructor of CallRef, with a note on the gate of
// the target.

#include <foundation/core/CallRef.h>

struct Region {};

int main() {
    auto takes_a_region = [](Region const&) noexcept { return 1; };
    ::foundation::core::CallRef<int(int) noexcept> const call_ref{takes_a_region};
    return call_ref(0);
}
