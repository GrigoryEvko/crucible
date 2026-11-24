// The mint of a keyed block of a named block.  The keyed block would share the
// bytes with the name of the caller, so a write through that name would reach
// a window of a frame.  The maker keyed_bytes_owner takes the block only by
// move, so the gate of the mint refuses the named block.

#include <foundation/AlignedBytes.h>
#include <foundation/alloc/Layout.h>
#include <foundation/effects/Effect.h>

int main() {
    ::foundation::AlignedBytes block = ::foundation::mint_aligned_bytes_zeroed(
        ::foundation::effects::testing::test().alloc,
        ::foundation::alloc::mint_layout(::foundation::core::ByteCount{64}, ::foundation::alloc::Alignment{64})
            .expect("a layout"));
    auto [keyed, key] = ::foundation::core::mint_keyed_owner(::foundation::keyed_bytes_owner, block);
    static_cast<void>(key);
    return keyed.size() == 64 ? 0 : 1;
}
