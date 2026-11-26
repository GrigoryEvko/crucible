// Negative-compile test.  This file must fail to compile.
//
// Product code holds the init context at startup, and it asks mint_context_key
// for a second key of a context.  With that key a body could change the mode of
// the context of a Vigil while the Vigil lends it.  Only the capability of a
// test opens the door, so the init context does not.  The fixture takes its init
// context from the testing namespace, so it names no door of an owner.

#include <crucible/CrucibleContext.h>

int main() {
    auto const startup = ::foundation::effects::testing::init();
    auto key = crucible::mint_context_key(startup);
    static_cast<void>(key);
    return 0;
}
