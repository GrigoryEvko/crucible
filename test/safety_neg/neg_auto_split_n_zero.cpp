// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-A23 — auto_split_n<Parent, 0> must be rejected.  Splitting a
// permission into ZERO shards has no operational meaning (no factory
// can deliver child permissions to anyone).  The static_assert inside
// auto_split_n catches the misuse at the instantiation site.
//
// Expected diagnostic substring:
//   "auto_split_n<Parent, N>: N must be greater than zero"

#include <crucible/safety/PermissionTreeGenerator.h>

namespace {
struct MyTag {};
}  // namespace

using BadSplit = crucible::safety::auto_split_n<MyTag, 0>::type;

// Force instantiation so the static_assert in auto_split_n's body
// fires.  Without this the template body is never instantiated and
// the assert stays dormant.
[[maybe_unused]] BadSplit dummy;

int main() { return 0; }
