// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A3-013 (#1620): Capability<E, S> has NO default ctor at any
// access level — it was replaced by an explicit ctor taking a
// cap_mint_key by value.  An arbitrary TU attempting to forge a
// Capability via `Capability<E, S>{}` must be rejected at the
// type-system level.
//
// Violation: forging a Capability via default-initialization.
//
// Expected diagnostic: "no matching function for call to
// 'Capability<...>::Capability()'" / "no default ctor" / "no
// matching function" — the only ctor signatures Capability exposes
// are `Capability(cap_mint_key)` and the defaulted move ctor; an
// empty initializer-list resolves to NEITHER.

#include <crucible/effects/Capability.h>

namespace eff = crucible::effects;

int main() {
    // Pre-A3-013 behavior: would have failed via "default ctor is
    // private" from inside Capability's class body.  Post-A3-013:
    // there's no default ctor at all, public or private — the
    // passkey ctor is the only public path, and CapMintKey is
    // outside the user's reach.
    eff::Capability<eff::Effect::Alloc, eff::Bg> c{};
    (void)c;
    return 0;
}
