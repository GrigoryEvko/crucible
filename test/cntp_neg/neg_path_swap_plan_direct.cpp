#include <crucible/cntp/PathSwap.h>

// mint_path_swap_plan is the only door to a plan, because it is the one
// place that refuses two equal paths.  A plan built with its constructor
// would skip that check, and the constructor is private.

int main() {
    namespace cntp = crucible::cntp;

    auto flow = cntp::admit_flow_id(cntp::FlowId{1}).expect("1 is positive");
    auto path = cntp::admit_path_id(cntp::PathId{2}).expect("2 is positive");
    auto other = cntp::admit_path_id(cntp::PathId{3}).expect("3 is positive");
    auto timeout = cntp::admit_swap_timeout(cntp::SwapNanoseconds{9}).expect("9 ns is positive");
    auto declared = cntp::mint_path_swap_plan(flow, cntp::OldPathId{path}, cntp::NewPathId{other}, timeout);
    (void)declared;
    cntp::PathSwapPlan forged{flow, cntp::OldPathId{path}, cntp::NewPathId{path}, timeout};
    (void)forged;
    return 0;
}
