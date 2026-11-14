// A pinned element keeps its address as its identity, and a growth of a
// GrowList moves each element to new storage.  So mint_grow_list refuses an
// element that derives from foundation::Pinned, also when the element writes
// a move of its own.  A growable set of pinned elements is a GrowList of
// Box<T>.

#include <foundation/Pinned.h>
#include <foundation/core/Grow.h>

struct Cell : ::foundation::Pinned<Cell> {
    int value = 0;
    Cell() noexcept = default;
    Cell(Cell&& other) noexcept : ::foundation::Pinned<Cell>{}, value{other.value} {}
};

int main() {
    auto cells = ::foundation::core::mint_grow_list<Cell>(8);
    return static_cast<int>(cells.size());
}
