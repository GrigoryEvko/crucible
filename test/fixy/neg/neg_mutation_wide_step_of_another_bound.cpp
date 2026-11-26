// A WideMonotonic steps only by a WideStep of its own bound.  A step of a
// larger bound can pass the bound of the count, so no step converts to a step
// of another bound.

#include <fixy/Mutation.h>

int main() {
    auto counter = ::fixy::mint_wide_monotonic<1>();
    counter.advance(::fixy::WideStep<2>::one());
    return static_cast<int>(counter.get());
}
