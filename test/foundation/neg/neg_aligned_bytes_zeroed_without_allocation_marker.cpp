// mint_aligned_bytes_zeroed takes the allocation capability first, so each
// heap allocation shows at its call.  A different capability tag fails the
// gate.

#include <foundation/AlignedBytes.h>
#include <foundation/effects/Effect.h>

int main() {
    auto block = ::foundation::mint_aligned_bytes_zeroed(::foundation::effects::testing::test().io,
                                                         ::foundation::alloc::Layout::of<unsigned long>());
    return static_cast<int>(block.size());
}
