// append moves its item into the log, and a pinned element keeps its address as
// its identity.  So append refuses an element that derives from
// foundation::Pinned, also when the element writes a move of its own.  A log
// takes such an element through append_with, which builds it in place.

#include <fixy/Mutation.h>
#include <foundation/Pinned.h>
#include <foundation/effects/Effect.h>

struct Cell : ::foundation::Pinned<Cell> {
    int value = 0;
    Cell() noexcept = default;
    Cell(Cell&& other) noexcept : ::foundation::Pinned<Cell>{}, value{other.value} {}
};

int main() {
    auto log = fixy::mint_append_only<Cell>();
    log.append(::foundation::effects::testing::test().alloc, Cell{});
    return static_cast<int>(log.size());
}
