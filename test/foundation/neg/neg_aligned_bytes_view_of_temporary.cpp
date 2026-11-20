// A View of the block that the zeroed mint gives as a temporary.  The full
// expression gives the block back, so the View would dangle.  The overload
// for an rvalue is deleted.

#include <foundation/AlignedBytes.h>
#include <foundation/effects/Effect.h>

int main() {
    auto bytes = ::foundation::mint_aligned_bytes_zeroed(::foundation::effects::testing::test().alloc, 64, 64).view();
    return static_cast<int>(bytes.size());
}
