#include <crucible/cntp/PathSwap.h>

// The step check takes the state that a step leaves and the state that it
// enters in two roles, so a call that gives them in the other order does not
// compile.  With one type for the two states, the check would admit Draining
// to Stable, the reverse of the step from Stable to Draining.

int main() {
    namespace cntp = crucible::cntp;

    cntp::SwapFromState const from{cntp::SwapState::Stable};
    cntp::SwapToState const to{cntp::SwapState::Draining};
    bool const is_valid = cntp::is_valid_path_swap_transition(to, from);
    return is_valid ? 0 : 1;
}
