#include <crucible/cntp/PathSwap.h>
#include <fixy/Ctx.h>

// begin_swap takes a plan under the PathSwap tag.  A bare PathSwapPlan, even
// one read out of a declared plan, has no conversion to the tagged plan.

int main() {
    namespace cntp = crucible::cntp;
    namespace fe = ::foundation::effects;

    ::fixy::ColdInitCtx init{fe::testing::init()};
    ::fixy::BgDrainCtx bg{fe::testing::bg()};
    auto swapper = cntp::mint_path_swapper(init);
    auto declared = cntp::mint_path_swap_plan(cntp::admit_path_id(1).value(), cntp::admit_path_id(2).value(),
                                              cntp::admit_path_id(3).value(), cntp::admit_swap_timeout_ns(9).value());
    cntp::PathSwapPlan raw = declared.value().value();
    auto result = swapper.begin_swap(bg, raw, 0);
    (void)result;
    return 0;
}
