// The bound of one step of a WideMonotonic is at least one.  A bound of zero
// gives a count that no step moves, so the constraint of the mint refuses it.

#include <fixy/Mutation.h>

int main() {
    auto counter = ::fixy::mint_wide_monotonic<0>();
    return static_cast<int>(counter.get());
}
