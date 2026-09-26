// A copy assignment from an older bounded counter would move a counter
// backward.  BoundedMonotonic deletes both assignments, so the copy
// assignment below names a deleted function.

#include <fixy/Mutation.h>

#include <cstdint>

int main() {
    auto older = fixy::mint_bounded_monotonic<std::uint32_t, 1024u>(1u);
    auto counter = fixy::mint_bounded_monotonic<std::uint32_t, 1024u>(900u);
    counter = older;
    return static_cast<int>(counter.get() + older.get());
}
