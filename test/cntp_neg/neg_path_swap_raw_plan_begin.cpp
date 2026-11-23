#include <crucible/cntp/PathSwap.h>
#include <fixy/Ctx.h>

// begin_swap takes a plan under the PathSwap tag.  A bare PathSwapPlan, even
// one read out of a declared plan, has no conversion to the tagged plan.

int main() {
    namespace cntp = crucible::cntp;
    namespace fe = ::foundation::effects;

    ::fixy::ColdInitCtx init{fe::testing::init()};
    ::fixy::BgLoadCtx bg{fe::testing::bg()};
    auto swapper = cntp::mint_path_swapper(init);
    cntp::DeclaredPathSwapPlan const declared =
        cntp::mint_path_swap_plan(cntp::admit_flow_id(cntp::FlowId{1}).expect("1 is positive"),
                                  cntp::OldPathId{cntp::admit_path_id(cntp::PathId{2}).expect("2 is positive")},
                                  cntp::NewPathId{cntp::admit_path_id(cntp::PathId{3}).expect("3 is positive")},
                                  cntp::admit_swap_timeout(cntp::SwapNanoseconds{9}).expect("9 ns is positive"))
            .expect("the two paths differ");
    cntp::PathSwapPlan raw = declared.value();
    auto result = swapper.begin_swap(bg, raw, cntp::SwapNanoseconds{0});
    (void)result;
    return 0;
}
