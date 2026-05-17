// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-EFF fixture #2: CanMintCap rejects when the source ctx-cap-tag
// does not admit the requested Effect.
//
// Violation: `ctx_cap::Fg` (foreground) has cap_permitted_row ==
// Row<>, so CanMintCap<Effect::Block, Fg> is false.  Asserting the
// concept holds is a hard compile error.
//
// Expected diagnostic: GCC's static_assert pointing at the
// "fixy::eff::CanMintCap<Block, Fg>" claim.

#include <crucible/fixy/Eff.h>

namespace fe = crucible::fixy::eff;

// Unique-carrier discipline.
struct EffNegFixture2_Marker {};

int main() {
    // Foreground cannot mint a Block capability — Fg's permitted row
    // is empty.  fixy alias passes the substrate's concept rejection
    // through identically.
    static_assert(fe::CanMintCap<fe::Effect::Block, fe::ctx_cap::Fg>,
        "fixy::eff::CanMintCap<Block, Fg> must reject — foreground "
        "context holds no capabilities.  Alias preserves the CanMintCap "
        "concept gate identically to the substrate.");
    (void)sizeof(EffNegFixture2_Marker);
    return 0;
}
