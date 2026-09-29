#include <crucible/cntp/PathSwap.h>
#include <fixy/Ctx.h>

// A swapper is built at startup, in a context that owns Init.  The
// background drain context owns Bg and no Init, so the mint refuses it.

int main() {
    namespace cntp = crucible::cntp;
    namespace fe = ::foundation::effects;

    ::fixy::BgDrainCtx bg{fe::testing::bg()};
    auto swapper = cntp::mint_path_swapper(bg);
    (void)swapper;
    return 0;
}
