#include <crucible/cntp/PathSwap.h>

// GAPS-122 fixture #1: the path-swap state machine starts only from a
// tagged DeclaredPathSwapPlan. Raw path IDs cannot directly drive a
// live transport-resource transition.

int main() {
    crucible::effects::ColdInitCtx init{};
    crucible::effects::BgDrainCtx bg{};
    auto swapper = crucible::cntp::mint_path_swapper(init);
    crucible::cntp::PathSwapPlan raw{};
    auto result = swapper.begin_swap(bg, raw, 0);
    (void)result;
    return 0;
}
