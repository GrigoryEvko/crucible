// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A3-005 (#1612): effects::Init's default constructor is private —
// only the minter `mint_init_context(detail::ctx_mint::init_key)` can
// fabricate one.  Vigil and BackgroundThread are friended on init_key.
// An unprivileged TU that writes `eff::Init init{};` must fail to
// compile.
//
// Mirrors the H-25 passkey-via-passkey pattern.  Without this guard,
// any TU can fabricate the Init capability that gates Senses /
// DeadlineWatchdog construction and other long-running setup work,
// breaking the design invariant that Init must come from the runtime
// authority (Vigil::start).
//
// Expected diagnostic: "private within this context" /
// "'Init' has been explicitly marked as private" / "fixy-A3-005".

#include <crucible/effects/Capabilities.h>

namespace eff = crucible::effects;

int main() {
    eff::Init init{};            // <-- this MUST fail (private ctor)
    (void)init;
    return 0;
}
