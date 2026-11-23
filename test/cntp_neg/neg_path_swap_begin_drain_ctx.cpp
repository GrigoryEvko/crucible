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
    auto plan = cntp::mint_path_swap_plan(cntp::admit_flow_id(cntp::FlowId{1}).expect("1 is positive"),
                                          cntp::OldPathId{cntp::admit_path_id(cntp::PathId{2}).expect("2 is positive")},
                                          cntp::NewPathId{cntp::admit_path_id(cntp::PathId{3}).expect("3 is positive")},
                                          cntp::admit_swap_timeout(cntp::SwapNanoseconds{9}).expect("9 ns is positive"))
                    .expect("the two paths differ");
    auto result = swapper.begin_swap(drain, plan, cntp::SwapNanoseconds{0});
    (void)result;
    return 0;
}
