// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A class with no friendship tries to reach the handle factory by
// deriving from it, to call a member from its own scope or to make one
// public with a using-declaration.  The factory is final, so the
// derivation itself is refused, before a member is named.
//
// Expected diagnostic: a final base cannot be derived from.

#include <fixy/session/Handle.h>

namespace {
struct LookAlike : ::fixy::session::HandleFactory {};
}  // namespace

int main() { return 0; }
