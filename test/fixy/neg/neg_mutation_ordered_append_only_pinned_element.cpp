// A growth of an ordered log moves each element, and a pinned element keeps its
// address as its identity.  So the door of the log refuses an element that
// derives from foundation::Pinned.

#include <fixy/Mutation.h>
#include <foundation/Pinned.h>

struct Cell : ::foundation::Pinned<Cell> {
    int value = 0;
    Cell() noexcept = default;
    Cell(Cell&& other) noexcept : ::foundation::Pinned<Cell>{}, value{other.value} {}
};

struct ValueOfCell {
    [[nodiscard]] constexpr int operator()(Cell const& cell) const noexcept { return cell.value; }
};

int main() {
    auto log = fixy::mint_ordered_append_only<Cell, ValueOfCell>();
    return static_cast<int>(log.size());
}
