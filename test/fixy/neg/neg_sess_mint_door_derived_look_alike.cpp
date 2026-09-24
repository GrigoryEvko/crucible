// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A class with no friendship tries to reach the door of the session
// mints by deriving from it.  A member of that class could then call a
// door member, or build a forked endpoint that opens a session on a
// record the caller names.  The door is final, so the derivation itself
// is refused.
//
// Expected diagnostic: a final base cannot be derived from.

#include <fixy/session/Handle.h>

namespace {
struct LookAlike : ::fixy::session::SessionMintDoor {};
}  // namespace

int main() {
    return 0;
}
