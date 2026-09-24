// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A class with no friendship tries to get access to the door of the
// crash mint through derivation.  A member of that class can then put a
// handle under crash-stop semantics for a protocol that the mint
// rejects.  The door is final.  The compiler rejects the derivation
// before the code uses a member.
//
// Expected diagnostic: a final base cannot be derived from.

#include <fixy/session/CrashTransport.h>

namespace {
struct LookAlike : ::fixy::session::CrashSessionDoor {};
}  // namespace

int main() {
    return 0;
}
