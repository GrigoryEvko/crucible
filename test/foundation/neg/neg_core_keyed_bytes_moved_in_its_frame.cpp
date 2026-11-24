// A body of a frame of the windows of a keyed block reaches the block by a
// name.  A move out of that name would end the bytes under the ViewMuts of the
// frame, so a keyed block does not move.

#include <foundation/AlignedBytes.h>
#include <foundation/alloc/Layout.h>
#include <foundation/core/KeyedMint.h>
#include <foundation/effects/Effect.h>

int main() {
    namespace core = ::foundation::core;
    auto [block, key] = core::mint_keyed_owner(
        ::foundation::keyed_bytes_owner,
        ::foundation::mint_aligned_bytes_zeroed(
            ::foundation::effects::testing::test().alloc,
            ::foundation::alloc::mint_layout(core::ByteCount{64}, ::foundation::alloc::Alignment{64})
                .expect("the fixture gives a layout")));
    core::FixedArray<::foundation::ByteWindow, 1> const windows{
        ::foundation::ByteWindow{core::side_as<core::side::WindowStart>(core::ByteCount{0}),
                                 core::side_as<core::side::WindowLength>(core::ByteCount{64})}};
    auto const written = ::foundation::with_mut_windows<int>(block, key, windows, [&moved = block](auto view) noexcept {
        auto const taken = std::move(moved);
        for (int& element : view)
            element = 1;
        return taken.size() == 64;
    });
    return written.is_ok() ? 0 : 1;
}
