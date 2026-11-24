// A keyed FixedHeapArray has no value door: a copy into a window through
// another name would write the run while a frame of the array is open.

#include <foundation/core/FixedHeapArray.h>
#include <foundation/core/KeyedMint.h>
#include <foundation/effects/Effect.h>

int main() {
    auto [words, key] = ::foundation::core::mint_keyed_owner(::foundation::core::fixed_heap_array_owner<int, 4>,
                                                             ::foundation::effects::testing::test().alloc, 3);
    words.with_mut(key, [&written = words](auto run) noexcept {
        written.copy_window(0, ::foundation::core::View<int const>{run.view()});
    });
    return words.at(::foundation::core::index<0>());
}
