// A body reborrows a named ViewMut.  The name would stay with the body, so
// the ViewMut of the inner body would be a second writer of the run beside
// it.  A reborrow takes its ViewMut by move.

#include <foundation/core/Array.h>

#include <utility>

int main() {
    auto [array, key] = ::foundation::core::mint_keyed_owner(::foundation::core::fixed_array_owner<int, 4>,
                                                             ::foundation::core::FixedArray<int, 4>{1, 2, 3, 4});
    array.with_mut(key, [](auto out) noexcept {
        ::foundation::core::reborrow(out, [](auto lent) noexcept { ::foundation::core::fill(std::move(lent), 0); });
    });
    return 0;
}
