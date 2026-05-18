// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-H-25 fixture #2: a user CANNOT forge a `crash_witness_key` from
// outside the bridge layer.  The key's ctor is private, friended only
// to `bridges::wrap_crash_return` — so attempting `crash_witness_key{}`
// from user code (here, `main`) must fail.
//
// This is the orthogonal failure mode to fixture #1: where #1 covers
// "call the factory with no key at all", #2 covers "try to forge the
// key directly".  Together they witness BOTH sides of the H-25 gate:
//   1. The factory requires a witness.
//   2. The witness cannot be minted from arbitrary scope.
//
// Violation: direct `crash_witness_key{}` construction from main.
//
// Expected diagnostic: "is private" / "private within this context" /
// "private member" / "ctor.*private".

#include <crucible/permissions/PermissionInherit.h>

namespace pi = ::crucible::permissions;

struct DeadPeerTag {};
struct SurvivorTag {};

template <>
struct pi::survivor_registry<DeadPeerTag> {
    using type = pi::inheritance_list<SurvivorTag>;
};

int main() {
    // BAD: crash_witness_key has a private default ctor.  Only
    // bridges::wrap_crash_return is friended on it.  This TU is not
    // a friend, so construction here is rejected.
    pi::crash_witness_key forged_key{};
    auto bad = pi::mint_permission_inherit<DeadPeerTag, SurvivorTag>(forged_key);
    (void)bad;
    return 0;
}
