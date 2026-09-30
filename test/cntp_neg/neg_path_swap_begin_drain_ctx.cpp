#include <crucible/cntp/PathSwap.h>
#include <fixy/Ctx.h>

// A transition waits on the gate of the swapper, and a wait on that gate is
// a block.  The drain context owns Bg and no Block, so it cannot start a
// swap.

int main() {
    namespace cntp = crucible::cntp;
    namespace fe = ::foundation::effects;

    ::fixy::ColdInitCtx init{fe::testing::init()};
    ::fixy::BgDrainCtx drain{fe::testing::bg()};
    auto swapper = cntp::mint_path_swapper(init);
    auto plan = cntp::mint_path_swap_plan(cntp::admit_path_id(1).value(), cntp::admit_path_id(2).value(),
                                          cntp::admit_path_id(3).value(), cntp::admit_swap_timeout_ns(9).value());
    auto result = swapper.begin_swap(drain, plan.value(), 0);
    (void)result;
    return 0;
}
