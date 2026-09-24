// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-H-25 fixture #1: `mint_permission_inherit<DeadTag, SurvivorTags...>()`
// can no longer be called without a `crash_witness_key` parameter.
//
// Pre-H-25 the factory took no arguments — anyone could mint survivor
// permissions any time, without proving the legitimate holder of
// `Permission<DeadTag>` was dead.  H-25 added a passkey parameter
// (`crash_witness_key`) that ONLY `bridges::wrap_crash_return` can
// produce (private ctor, friend-gated).  Direct calls from anywhere
// else are rejected as "too few arguments".
//
// Violation: omit the witness entirely.
//
// Expected diagnostic: "too few arguments" /
// "no matching function for call" / "mint_permission_inherit".

#include <crucible/permissions/PermissionInherit.h>

namespace pi = ::crucible::permissions;

// A pair of opt-in inheritance tags so the function template
// substitution proceeds past the "no survivors" static_assert and the
// real "no witness" diagnostic surfaces.
struct DeadPeerTag {};
struct SurvivorTag {};

template <>
struct pi::survivor_registry<DeadPeerTag> {
    using type = pi::inheritance_list<SurvivorTag>;
};

int main() {
    // BAD: no crash_witness_key passed.  Pre-H-25 this compiled; post-
    // H-25 the parameter is mandatory.
    auto bad = pi::mint_permission_inherit<DeadPeerTag, SurvivorTag>();
    (void)bad;
    return 0;
}
