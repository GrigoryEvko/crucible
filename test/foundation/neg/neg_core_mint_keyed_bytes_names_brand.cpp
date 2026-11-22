// A caller names the brand of mint_keyed_bytes.  A key of a brand that the
// caller holds would open the windows of each block of that brand, so the
// pack of the mint takes each explicit argument, and the gate of the mint
// refuses it.

#include <foundation/AlignedBytes.h>
#include <foundation/alloc/Layout.h>
#include <foundation/effects/Effect.h>

namespace {

struct HeldBrand final {};

}  // namespace

int main() {
    auto [keyed, key] = ::foundation::mint_keyed_bytes<HeldBrand>(::foundation::mint_aligned_bytes_zeroed(
        ::foundation::effects::testing::test().alloc,
        ::foundation::alloc::mint_layout(::foundation::core::ByteCount{64}, ::foundation::alloc::Alignment{64})
            .expect("a layout")));
    static_cast<void>(key);
    return keyed.size() == 64 ? 0 : 1;
}
