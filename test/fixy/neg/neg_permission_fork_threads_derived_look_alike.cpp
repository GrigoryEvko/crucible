// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A class with no friendship tries to reach the body of the fork by
// deriving from PermissionForkThreads, so that it could declare the body
// public again with a using-declaration or call it from a member.  The
// class is final, so the derivation itself is refused, before any member
// is named.  A using-declaration of the body needs such a derived class,
// so the same refusal closes that route too.
//
// Expected diagnostic: a final base cannot be derived from.

#include <fixy/concurrent/PermissionFork.h>

namespace {
struct LookAlike : ::fixy::concurrent::PermissionForkThreads {};
}  // namespace

int main() { return 0; }
