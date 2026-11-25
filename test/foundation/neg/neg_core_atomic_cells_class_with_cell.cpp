// share_atomic_cells lends a View only of the cells of the base.  A class
// that holds an Atomic is not such a cell: the class can add a plain member
// or an assignment, and a write to it through the View then races.

#include <foundation/core/Array.h>
#include <foundation/core/AtomicCells.h>

namespace {

struct CountedSlot {
    ::foundation::core::Atomic<int> count{};
};

}  // namespace

int main() {
    auto [array, key] = ::foundation::core::mint_keyed_owner(::foundation::core::fixed_array_owner<CountedSlot, 4>);
    return array.with_mut(key, [](auto run) noexcept {
        ::foundation::core::View<CountedSlot, 4> const shared = ::foundation::core::share_atomic_cells(run);
        return static_cast<int>(shared.size());
    });
}
