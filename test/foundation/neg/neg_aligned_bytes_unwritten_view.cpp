// The bytes of a new block hold no value until a write.  A sum of them read
// the fill of the allocator: 64 bytes of 0xbe gave 12160 under
// AddressSanitizer, with no report, and the bytes of an earlier use of the
// heap in a Release build.  mint_aligned_bytes gives an UnwrittenBytes, and
// its view is deleted, so the sum does not compile.

#include <foundation/AlignedBytes.h>
#include <foundation/effects/Effect.h>

#include <cstdint>

int main() {
    ::foundation::UnwrittenBytes block =
        ::foundation::mint_aligned_bytes(::foundation::effects::testing::test().alloc, 64, 64);
    unsigned sum = 0;
    for (std::uint8_t const byte : block.view()) {
        sum += byte;
    }
    return sum == 0 ? 0 : 1;
}
