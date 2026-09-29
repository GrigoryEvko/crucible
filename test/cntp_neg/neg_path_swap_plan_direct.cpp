#include <crucible/cntp/PathSwap.h>

// mint_path_swap_plan is the only door to a plan, because it is the one
// place that refuses two equal paths.  A plan built with its constructor
// would skip that check, and the constructor is private.

int main() {
    namespace cntp = crucible::cntp;

    auto flow = cntp::admit_path_id(1).value();
    auto path = cntp::admit_path_id(2).value();
    auto timeout = cntp::admit_swap_timeout_ns(9).value();
    auto declared = cntp::mint_path_swap_plan(flow, path, cntp::admit_path_id(3).value(), timeout);
    (void)declared;
    cntp::PathSwapPlan forged{flow, path, path, timeout};
    (void)forged;
    return 0;
}
