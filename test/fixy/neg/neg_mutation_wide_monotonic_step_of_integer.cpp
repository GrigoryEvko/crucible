// A step of a WideMonotonic of a plain integer has no bound in its type, so
// it can wrap the count.  The step is deleted.

#include <fixy/Mutation.h>

#include <cstdint>

int main() {
    auto counter = ::fixy::mint_wide_monotonic<1>();
    counter.step(std::uint64_t{2});
    return static_cast<int>(counter.get());
}
