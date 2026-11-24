// Two frames of one block would give two ViewMuts of two brands over one run,
// and a copy between them would take a memcpy on bytes that overlap.  The
// frame door of the windows takes the block by move, so a body that opens a
// second frame on the name of the block does not compile.

#include <foundation/AlignedBytes.h>
#include <foundation/alloc/Layout.h>
#include <foundation/effects/Effect.h>

int main() {
    namespace core = ::foundation::core;
    auto block = ::foundation::mint_aligned_bytes_zeroed(
        ::foundation::effects::testing::test().alloc,
        ::foundation::alloc::mint_layout(core::ByteCount{256}, ::foundation::alloc::Alignment{64})
            .expect("the fixture gives a layout"));
    core::FixedArray<::foundation::ByteWindow, 1> const source{
        ::foundation::ByteWindow{core::side_as<core::side::WindowStart>(core::ByteCount{0}),
                                 core::side_as<core::side::WindowLength>(core::ByteCount{252})}};
    core::FixedArray<::foundation::ByteWindow, 1> const target{
        ::foundation::ByteWindow{core::side_as<core::side::WindowStart>(core::ByteCount{4}),
                                 core::side_as<core::side::WindowLength>(core::ByteCount{252})}};
    auto const copied = ::foundation::with_mut_windows<int>(block, source, [&block, &target](auto from) noexcept {
        return ::foundation::with_mut_windows<int>(
                   block, target, [&from](auto into) noexcept { return core::copy(std::move(into), from).is_ok(); })
            .is_ok();
    });
    return copied.is_ok() ? 0 : 1;
}
