// A KeyedHeapArray has no value door.  A body of a keyed door can reach the
// holder through a capture, and a value door with no key would let it write
// the storage that the body holds a reference into.

#include <foundation/core/Array.h>
#include <foundation/core/KeyedArray.h>
#include <foundation/core/KeyedMint.h>
#include <foundation/effects/Effect.h>

namespace core = ::foundation::core;

int main() {
    auto [words, key] = core::mint_keyed_owner(
        core::keyed_heap_array_owner<int>, core::mint_heap_array(::foundation::effects::testing::test().alloc, 4, 1));
    words.set(2, 3);
    return static_cast<int>(words.size());
}
