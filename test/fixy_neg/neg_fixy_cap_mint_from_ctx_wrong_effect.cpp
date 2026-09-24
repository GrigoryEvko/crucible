// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-MINT-Cap fixture #2: mint_from_ctx via fixy:: alias rejects
// when the requested Effect is not in Ctx::row_type.
//
// Violation: HotFgCtx's row is Row<> (foreground holds NO
// capabilities — every effect must be claimed by some bg/cold ctx
// upstream).  `mint_from_ctx<Effect::Alloc>(hot_fg)` therefore fails
// CtxCanMint<HotFgCtx, Alloc>.  Routing through `fixy::cap::mint_from_ctx`
// must reject identically.
//
// Expected diagnostic: GCC's "associated constraints are not
// satisfied" pointing at CtxCanMint.

#include <crucible/fixy/Cap.h>

namespace eff = crucible::effects;
namespace cap = crucible::fixy::cap;

int main() {
    eff::HotFgCtx hot_fg{};
    auto bad = cap::mint_from_ctx<eff::Effect::Alloc>(hot_fg);  // CtxCanMint fails
    (void)bad;
    return 0;
}
