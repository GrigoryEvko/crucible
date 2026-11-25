// A body copies the View of its ViewMut and writes an element through the
// copy.  The View of a ViewMut reads and does not write, so no copy of it is a
// second writer of the run.  get_mut gives the writer of an element.

#include <foundation/core/Array.h>

int main() {
    auto [array, key] = ::foundation::core::mint_keyed_owner(::foundation::core::fixed_array_owner<int, 4>, ::foundation::core::FixedArray<int, 4>{1, 2, 3, 4});
    array.with_mut(key, [](auto run) noexcept {
        auto const first = run.view();
        auto second = first;
        second.at(::foundation::core::index<0>()) = 5;
    });
    return 0;
}
