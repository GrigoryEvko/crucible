// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A class with no friendship tries to get access to the door of the
// recording mint through derivation.  A member of that class can then
// put a recorder around a value that the mint rejects.  The door is
// final.  The compiler rejects the derivation before the code uses a
// member.
//
// Expected diagnostic: a final base cannot be derived from.

#include <fixy/session/Recording.h>

namespace {
struct LookAlike : ::fixy::session::RecordingDoor {};
}  // namespace

int main() { return 0; }
