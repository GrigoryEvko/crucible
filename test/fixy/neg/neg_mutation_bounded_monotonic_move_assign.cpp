// An assignment from a fresh mint would move a bounded counter backward.
// BoundedMonotonic deletes both assignments, so the move assignment below
// names a deleted function.

#include <fixy/Mutation.h>

#include <cstdint>

int main() {
    auto counter = fixy::mint_bounded_monotonic<std::uint32_t, 1024u>(10u);
    counter.advance(900u);
    counter = fixy::mint_bounded_monotonic<std::uint32_t, 1024u>(0u);
    return static_cast<int>(counter.get());
}
