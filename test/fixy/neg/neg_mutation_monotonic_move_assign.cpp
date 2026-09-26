// An assignment from a fresh mint would move a counter backward without
// reset_under_quiescence.  Monotonic deletes both assignments, so the
// move assignment below names a deleted function.

#include <fixy/Mutation.h>

#include <cstdint>

int main() {
    auto counter = fixy::mint_monotonic<std::uint64_t>(100u);
    counter.advance(200u);
    counter = fixy::mint_monotonic<std::uint64_t>(0u);
    return static_cast<int>(counter.get());
}
