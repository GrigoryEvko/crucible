// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// P1-AUDIT-3 (#854) + fixy-A3-013 passkey refactor: Capability has
// NO default ctor.  The only public-shape constructor is
// `explicit Capability(cap_mint_key)`; `cap_mint_key` is a passkey
// type only constructible by `mint_cap<>()` (templated-friend
// pattern; see Capability.h line 187).
//
// Violation: forging a Capability<E, S> by direct default-
// construction would bypass the CanMintCap authorization gate.
// Because no default ctor exists (and the passkey ctor takes one
// argument), the brace-enclosed initializer list `{}` triggers
// "no matching function for call to Capability(...)" — the overload
// set contains only the passkey ctor + the implicit move ctor, both
// requiring exactly one argument.
//
// Pre-FIXY-U-125 this fixture documented + searched-for a "private
// default ctor" mental model that the substrate shed at fixy-A3-013
// (cap_mint_key passkey replaced the private-default-ctor /
// templated-friend pattern).  Doc-block + CMake regex updated to
// match the current passkey-only surface; the fixture's security
// claim (default-construction forging Capability is impossible) is
// preserved — only the diagnostic family changed.
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "no matching function" / "candidate expects 1 argument" /
//   "cap_mint_key".

#include <crucible/effects/Capability.h>

namespace eff = crucible::effects;

int main() {
    // No default ctor — overload resolution fails.  Only
    // `Capability(cap_mint_key)` and the implicit move ctor exist,
    // both requiring one argument; brace-init with zero arguments
    // matches neither.
    eff::Capability<eff::Effect::Alloc, eff::Bg> bad{};
    (void)bad;
    return 0;
}
