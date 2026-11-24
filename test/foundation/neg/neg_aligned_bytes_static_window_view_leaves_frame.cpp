// The result of a frame door leaves the frame.  A body of the static form of
// with_mut_windows that gives the Ref of an element of a window gives a
// borrow that names the brand of the frame, so the door refuses the body.

#include <foundation/AlignedBytes.h>
#include <foundation/alloc/Layout.h>
#include <foundation/effects/Effect.h>

int main() {
    namespace core = ::foundation::core;
    auto block = ::foundation::mint_aligned_bytes_zeroed(
        ::foundation::effects::testing::test().alloc,
        ::foundation::alloc::mint_layout(core::ByteCount{64}, ::foundation::alloc::Alignment{64})
            .expect("the fixture gives a layout"));
    core::FixedArray<::foundation::ByteWindow, 1> const windows{
        ::foundation::ByteWindow{core::side_as<core::side::WindowStart>(core::ByteCount{0}),
                                 core::side_as<core::side::WindowLength>(core::ByteCount{16})}};
    auto kept = ::foundation::with_mut_windows<float>(std::move(block), windows,
                                                      [](auto only) noexcept { return only.get(0); });
    return kept.size() == 2 ? 0 : 1;
}
