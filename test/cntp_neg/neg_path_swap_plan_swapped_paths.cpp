#include <crucible/cntp/PathSwap.h>

// mint_path_swap_plan takes the old path and the new path in two roles, so
// a call that gives them in the other order does not compile.  With one type
// for the two paths, the plan would move the flow back to the path it left.

int main() {
    namespace cntp = crucible::cntp;

    auto flow = cntp::admit_flow_id(cntp::FlowId{1}).expect("1 is positive");
    cntp::OldPathId const old_path{cntp::admit_path_id(cntp::PathId{2}).expect("2 is positive")};
    cntp::NewPathId const new_path{cntp::admit_path_id(cntp::PathId{3}).expect("3 is positive")};
    auto timeout = cntp::admit_swap_timeout(cntp::SwapNanoseconds{9}).expect("9 ns is positive");
    auto plan = cntp::mint_path_swap_plan(flow, new_path, old_path, timeout);
    (void)plan;
    return 0;
}
