// Negative-compile test.  This file must fail to compile.
//
// A HeldVigil proves that the calling thread holds the live Vigil of a
// handle, so only the claim door of a slot of the table of handles makes one
// (mint_held_vigil of vessel/torch/vigil_handles.h).  A construction from a
// handle word, which a C caller gives, would prove nothing, and the private
// constructor refuses it.

#include "../../vessel/torch/vigil_handles.h"

int main() {
    const crucible::vessel::HeldVigil forged{crucible::vessel::VigilHandle{1}};
    return forged.handle().raw() == 1 ? 0 : 1;
}
