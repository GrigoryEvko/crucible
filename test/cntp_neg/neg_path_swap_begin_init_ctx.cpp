#include <crucible/cntp/PathSwap.h>
#include <fixy/Ctx.h>

// A transition is background work.  The startup context owns Init and no
// Bg, so it can build a swapper but cannot start a swap.

int main() {
    namespace cntp = crucible::cntp;
    namespace fe = ::foundation::effects;

    ::fixy::ColdInitCtx init{fe::testing::init()};
    auto swapper = cntp::mint_path_swapper(init);
    auto plan = cntp::mint_path_swap_plan(cntp::admit_path_id(1).expect("1 is positive"),
                                          cntp::admit_path_id(2).expect("2 is positive"),
                                          cntp::admit_path_id(3).expect("3 is positive"),
                                          cntp::admit_swap_timeout_ns(9).expect("9 ns is positive"))
                    .expect("the two paths differ");
    auto result = swapper.begin_swap(init, plan, 0);
    (void)result;
    return 0;
}
