// mint_heap_array takes the allocation capability first, so each heap
// allocation shows at its call.  A different capability tag fails the gate.

#include <foundation/core/Array.h>
#include <foundation/effects/Effect.h>

int main() {
    auto numbers = ::foundation::core::mint_heap_array(::foundation::effects::IO{}, 4, 1);
    return static_cast<int>(numbers.size());
}
