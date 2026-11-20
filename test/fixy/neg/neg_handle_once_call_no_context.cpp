// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A thread that loses the race of Once::call spins until the winner ends,
// with no exit.  A spin with no exit carries Div, so the call takes the
// context of the caller, and the context owns Div.  A call with no context
// would spin from each context, the hot path included.
//
// Expected diagnostic: Once::call has no candidate that takes the callable
// alone.

#include <fixy/handle/Once.h>

namespace h = fixy::handle;

int main() {
    h::Once once{};
    once.call([]() noexcept {});
    return 0;
}
