// mint_aligned_bytes takes the allocation capability first, so each heap
// allocation shows at its call.  A different capability tag fails the gate.

#include <foundation/AlignedBuffer.h>
#include <foundation/effects/Effect.h>

int main() {
    auto block = ::foundation::mint_aligned_bytes(::foundation::effects::IO{}, 64, 64);
    return static_cast<int>(block.size());
}
