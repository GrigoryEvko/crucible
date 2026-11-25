// share_atomic_cells lends a View of the run of a frame beside its ViewMut.
// A plain write to an int through that View races with each other writer of
// the run, so a run of int has no shared view.

#include <foundation/core/Array.h>
#include <foundation/core/AtomicCells.h>

int main() {
    auto [array, key] = ::foundation::core::mint_keyed_owner(::foundation::core::fixed_array_owner<int, 4>);
    return array.with_mut(key, [](auto run) noexcept {
        ::foundation::core::View<int, 4> const shared = ::foundation::core::share_atomic_cells(run);
        return static_cast<int>(shared.size());
    });
}
