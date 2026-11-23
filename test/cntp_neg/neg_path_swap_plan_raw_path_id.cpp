#include <crucible/cntp/PathSwap.h>

// mint_path_swap_plan takes admitted path ids.  A raw integer has no
// conversion to the old path of a plan, so a zero id cannot reach a plan.

int main() {
    namespace cntp = crucible::cntp;

    auto flow = cntp::admit_flow_id(cntp::FlowId{1}).expect("1 is positive");
    auto new_path = cntp::admit_path_id(cntp::PathId{3}).expect("3 is positive");
    auto timeout = cntp::admit_swap_timeout(cntp::SwapNanoseconds{9}).expect("9 ns is positive");
    auto plan = cntp::mint_path_swap_plan(flow, std::uint64_t{0}, cntp::NewPathId{new_path}, timeout);
    (void)plan;
    return 0;
}
