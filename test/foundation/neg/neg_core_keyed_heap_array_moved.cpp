// A KeyedHeapArray does not move.  A move would give a second holder of the
// storage, and a body of a keyed door could end the first holder through it.
// The holder gives its array away only with take and its key.

#include <foundation/core/Array.h>
#include <foundation/core/KeyedArray.h>
#include <foundation/core/KeyedMint.h>
#include <foundation/effects/Effect.h>

#include <utility>

namespace core = ::foundation::core;

int main() {
    auto [words, key] = core::mint_keyed_owner(
        core::keyed_heap_array_owner<int>, core::mint_heap_array(::foundation::effects::testing::test().alloc, 4, 1));
    auto other{std::move(words)};
    return static_cast<int>(other.size());
}
