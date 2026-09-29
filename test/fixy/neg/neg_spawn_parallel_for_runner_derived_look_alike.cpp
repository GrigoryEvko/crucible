// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A class with no friendship tries to reach the fan-out of
// mint_parallel_for by deriving from ParallelForRunner, so that it could
// re-declare the fan-out public with a using-declaration or call it from a
// member.  The runner is final, so the derivation itself is refused,
// before any member is named.
//
// Expected diagnostic: a final base cannot be derived from.

#include <fixy/os/Spawn.h>

namespace {
struct LookAlike : fixy::spawn::ParallelForRunner {};
}  // namespace

int main() { return 0; }
