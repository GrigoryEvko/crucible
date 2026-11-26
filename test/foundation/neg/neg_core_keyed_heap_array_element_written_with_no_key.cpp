// A KeyedHeapArray lends an element that a body writes only with its key.  A
// body of a keyed door can reach the holder through a Ref, and a door with no
// key would let it lend the same element a second time.

#include <foundation/core/Array.h>
#include <foundation/core/KeyedArray.h>
#include <foundation/core/KeyedMint.h>
#include <foundation/effects/Effect.h>

namespace core = ::foundation::core;

int main() {
    auto [words, key] = core::mint_keyed_owner(
        core::keyed_heap_array_owner<int>, core::mint_heap_array(::foundation::effects::testing::test().alloc, 4, 1));
    words.with_element(2, [](int& word) noexcept { word = 3; }, [] noexcept {});
    return static_cast<int>(words.size());
}
