#include <crucible/cntp/PathSwap.h>
#include <fixy/Ctx.h>

// mint_path_swapper is the only door to a swapper, so a swapper exists only
// where a context that owns Init built it.  The default constructor is
// private.

int main() {
    namespace cntp = crucible::cntp;
    namespace fe = ::foundation::effects;

    ::fixy::ColdInitCtx init{fe::testing::init()};
    auto minted = cntp::mint_path_swapper<8>(init);
    (void)minted;
    cntp::PathSwapper<8> forged{};
    (void)forged;
    return 0;
}
