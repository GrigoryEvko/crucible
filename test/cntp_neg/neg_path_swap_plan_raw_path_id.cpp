#include <crucible/cntp/PathSwap.h>

// mint_path_swap_plan takes admitted path ids.  A raw integer has no
// conversion to PositivePathId, so a zero id cannot reach a plan.

int main() {
    namespace cntp = crucible::cntp;

    auto old_path = cntp::admit_path_id(2).value();
    auto new_path = cntp::admit_path_id(3).value();
    auto timeout = cntp::admit_swap_timeout_ns(9).value();
    auto plan = cntp::mint_path_swap_plan(std::uint64_t{0}, old_path, new_path, timeout);
    (void)plan;
    return 0;
}
