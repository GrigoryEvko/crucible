// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-MINT-Cap fixture #1: foreground ctx_cap mint via fixy:: alias.
//
// Violation: foreground (ctx_cap::Fg) has cap_permitted_row == Row<>,
// so CanMintCap<Effect::Alloc, Fg> is false.  Routing the mint
// through `fixy::cap::mint_cap` must reject identically to the
// substrate `effects::mint_cap` — proves the fixy:: alias preserves
// the substrate's requires-clause discipline (no second-source mint
// authority per CLAUDE.md §XXI).
//
// Expected diagnostic: GCC's "associated constraints are not
// satisfied" pointing at CanMintCap.

#include <crucible/fixy/Cap.h>

namespace eff = crucible::effects;
namespace cap = crucible::fixy::cap;

int main() {
    eff::ctx_cap::Fg fg;
    auto bad = cap::mint_cap<eff::Effect::Alloc>(fg);  // CanMintCap fails via fixy:: alias
    (void)bad;
    return 0;
}
