// mint_heap_array_copy takes the allocation capability first, so each heap
// allocation shows at its call.  A different capability tag fails the gate.

#include <foundation/core/Array.h>
#include <foundation/effects/Effect.h>

int main() {
    auto const source = ::foundation::core::mint_heap_array(::foundation::effects::testing::test().alloc, 4, 1);
    auto copied =
        ::foundation::core::mint_heap_array_copy<int>(::foundation::effects::testing::test().io, source.view());
    return static_cast<int>(copied.size());
}
