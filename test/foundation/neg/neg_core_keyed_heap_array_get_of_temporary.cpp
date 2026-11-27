// get of a KeyedHeapArray gives a Ref into the storage of the holder.  A
// holder that the full expression ends frees that storage, so get of an
// rvalue holder does not compile.

#include <foundation/core/Array.h>
#include <foundation/core/KeyedArray.h>
#include <foundation/core/KeyedMint.h>
#include <foundation/effects/Effect.h>

#include <utility>

namespace core = ::foundation::core;

int main() {
    auto [words, key] = core::mint_keyed_owner(
        core::keyed_heap_array_owner<int>, core::mint_heap_array(::foundation::effects::testing::test().alloc, 4, 1));
    return *std::move(words).get(0).expect("the holder holds four words");
}
