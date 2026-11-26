// Negative-compile test.  This file must fail to compile.
//
// The record path holds the foreground context of its Vigil, and it asks
// mint_context_key for a key of a context.  The Vigil keeps the one key of its
// context, so a second key from the record path could open that context.  Only
// the capability of a test opens the door, so the foreground context does not.

#include <crucible/CrucibleContext.h>

int main() {
    auto const& foreground = ::foundation::effects::testing::foreground<crucible::Vigil>();
    auto key = crucible::mint_context_key(foreground);
    static_cast<void>(key);
    return 0;
}
