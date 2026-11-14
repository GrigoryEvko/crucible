// mint_heap_array_zeroed takes the allocation capability first, so each heap
// allocation shows at its call.  A different capability tag fails the gate.

#include <foundation/core/Array.h>
#include <foundation/effects/Effect.h>

int main() {
    auto zeros = ::foundation::core::mint_heap_array_zeroed<int>(::foundation::effects::testing::test().io, 4);
    return static_cast<int>(zeros.size());
}
