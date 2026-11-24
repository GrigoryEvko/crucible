// The maker of a keyed FixedHeapArray takes the allocation capability first of
// its arguments.  Another capability is no allocation marker.

#include <foundation/core/FixedHeapArray.h>
#include <foundation/core/KeyedMint.h>
#include <foundation/effects/Effect.h>

int main() {
    auto [words, key] = ::foundation::core::mint_keyed_owner(::foundation::core::fixed_heap_array_owner<int, 4>,
                                                             ::foundation::effects::testing::test().io, 3);
    return words.at(::foundation::core::index<0>());
}
