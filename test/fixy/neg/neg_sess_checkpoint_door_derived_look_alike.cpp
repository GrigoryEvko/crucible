// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A class with no friendship tries to get access to the checkpoint door
// through derivation.  A member of that class can then open a checkpoint
// handle for a pair of protocols that the mint rejects, or build a plain
// handle at a position that no mint accepted.  The door is final.  The
// compiler rejects the derivation before the code uses a member.
//
// Expected diagnostic: a final base cannot be derived from.

#include <fixy/session/Checkpoint.h>

namespace {
struct LookAlike : ::fixy::session::CheckpointDoor {};
}  // namespace

int main() { return 0; }
