// A growth of a log moves each element, and a pinned element keeps its address
// as its identity.  So the door of the log refuses an element that derives from
// foundation::Pinned, also when the element writes a move of its own.

#include <fixy/Mutation.h>
#include <foundation/Pinned.h>

struct Cell : ::foundation::Pinned<Cell> {
    int value = 0;
    Cell() noexcept = default;
    Cell(Cell&& other) noexcept : ::foundation::Pinned<Cell>{}, value{other.value} {}
};

int main() {
    auto log = fixy::mint_append_only<Cell>();
    return static_cast<int>(log.size());
}
