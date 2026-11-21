// A WideMonotonic starts only from a WideStart.  A plain integer has no check
// of the start limit, so the mint does not take one.

#include <fixy/Mutation.h>

#include <cstdint>

int main() {
    auto counter = ::fixy::mint_wide_monotonic<1>(std::uint64_t{5});
    return static_cast<int>(counter.get());
}
