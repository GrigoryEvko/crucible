// A body of the keyed frame door of the windows of a block opens a second
// frame of the windows of the same block, with the key that it captures.  The
// two frames would lend two ViewMuts of two brands over one window.  The gate
// of the door refuses a body that reaches a key of the brand of the block.

#include <foundation/AlignedBytes.h>
#include <foundation/alloc/Layout.h>
#include <foundation/effects/Effect.h>

namespace fc = ::foundation::core;

int main() {
    auto [block, key] = fc::mint_keyed_owner(
        ::foundation::keyed_bytes_owner,
        ::foundation::mint_aligned_bytes_zeroed(
            ::foundation::effects::testing::test().alloc,
            ::foundation::alloc::mint_layout(fc::ByteCount{256}, ::foundation::alloc::Alignment{64})
                .expect("a layout")));
    fc::FixedArray<::foundation::ByteWindow, 1> const windows{::foundation::ByteWindow{
        fc::side_as<fc::side::WindowStart>(fc::ByteCount{0}), fc::side_as<fc::side::WindowLength>(fc::ByteCount{64})}};
    auto const outcome =
        ::foundation::with_mut_windows<int>(block, key, windows, [&block, &key, &windows](auto outer) noexcept {
            auto const inner =
                ::foundation::with_mut_windows<int>(block, key, windows, [](auto run) noexcept { return run.size(); });
            return outer.size() + (inner.is_ok() ? 1u : 0u);
        });
    return outcome.is_ok() ? 0 : 1;
}
