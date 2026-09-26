// A copy assignment from an older counter would move a counter backward.
// Monotonic deletes both assignments, so the copy assignment below names
// a deleted function.

#include <fixy/Mutation.h>

#include <cstdint>

int main() {
    auto older = fixy::mint_monotonic<std::uint64_t>(1u);
    auto counter = fixy::mint_monotonic<std::uint64_t>(100u);
    counter = older;
    return static_cast<int>(counter.get() + older.get());
}
