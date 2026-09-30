#include <crucible/cntp/PathSwap.h>
#include <fixy/Ctx.h>

// A read of the plan waits on the gate of the swapper.  The foreground
// context owns no Block, so it reads the state and not the plan.

int main() {
    namespace cntp = crucible::cntp;
    namespace fe = ::foundation::effects;

    ::fixy::ColdInitCtx init{fe::testing::init()};
    ::fixy::HotFgCtx fg{fe::testing::foreground()};
    auto swapper = cntp::mint_path_swapper(init);
    auto plan = swapper.plan(fg);
    return plan.has_value() ? 1 : 0;
}
