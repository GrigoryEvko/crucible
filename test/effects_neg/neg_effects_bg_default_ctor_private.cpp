// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A3-005 (#1612): effects::Bg's default constructor is private —
// only the minter `mint_bg_context(detail::ctx_mint::bg_key)` can
// fabricate one.  An unprivileged TU that writes `eff::Bg bg{};`
// must fail to compile.
//
// Mirrors the H-25 passkey-via-passkey pattern.  Without this guard,
// any TU can fabricate a Bg capability tag bypassing the boundary
// where the runtime authority lives (BackgroundThread::spawn) and
// silently inherit Alloc / IO / Block caps it was never granted.
//
// Expected diagnostic: "private within this context" /
// "'Bg' has been explicitly marked as private" / "fixy-A3-005".

#include <crucible/effects/Capabilities.h>

namespace eff = crucible::effects;

int main() {
    eff::Bg bg{};               // <-- this MUST fail (private ctor)
    (void)bg;
    return 0;
}
