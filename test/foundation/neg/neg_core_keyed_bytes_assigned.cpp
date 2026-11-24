// Two blocks of one mint site have one brand, so one could be assigned to the
// other in a frame of its windows, and the assignment would end the bytes
// under the ViewMuts of the frame.  A keyed block takes no assignment.

#include <foundation/AlignedBytes.h>
#include <foundation/alloc/Layout.h>
#include <foundation/core/KeyedMint.h>
#include <foundation/effects/Effect.h>

#include <utility>

namespace {

template <class Block>
void end_through_assignment(Block& kept, Block& other) noexcept {
    kept = std::move(other);
}

}  // namespace

int main() {
    namespace core = ::foundation::core;
    auto [block, key] = core::mint_keyed_owner(
        ::foundation::keyed_bytes_owner,
        ::foundation::mint_aligned_bytes_zeroed(
            ::foundation::effects::testing::test().alloc,
            ::foundation::alloc::mint_layout(core::ByteCount{64}, ::foundation::alloc::Alignment{64})
                .expect("the fixture gives a layout")));
    end_through_assignment(block, block);
    return block.size() == 64 ? 0 : 1;
}
