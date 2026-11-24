// A move of an array whose elements own storage writes the elements of the
// source.  A body of a frame of a keyed array could move the array out of its
// name under the ViewMut of the frame, so such an array does not move.

#include <foundation/core/Array.h>

#include <utility>

int main() {
    auto [array, key] = ::foundation::core::mint_keyed_owner(
        ::foundation::core::fixed_array_owner<::foundation::core::HeapArray<int>, 2>);
    std::size_t const taken = array.with_mut(key, [&moved = array](auto run) noexcept {
        auto const held = std::move(moved);
        return run.size() + held.size();
    });
    return taken == 4 ? 0 : 1;
}
