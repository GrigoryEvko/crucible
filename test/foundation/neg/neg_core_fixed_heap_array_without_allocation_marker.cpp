// mint_fixed_heap_array takes the allocation capability first, so each heap
// allocation shows at its call.  A different capability tag fails the gate.

#include <foundation/core/FixedHeapArray.h>
#include <foundation/effects/Effect.h>

int main() {
    auto numbers = ::foundation::core::mint_fixed_heap_array<int, 4>(::foundation::effects::testing::test().io, 1);
    return static_cast<int>(numbers.size());
}
