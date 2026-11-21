// The static form of with_mut_windows compares each pair of its windows, so
// it takes at most max_block_windows of them.  An array of nine windows
// binds to the deleted overload.

#include <foundation/AlignedBytes.h>
#include <foundation/alloc/Layout.h>
#include <foundation/effects/Effect.h>

int main() {
    namespace core = ::foundation::core;
    auto block = ::foundation::mint_aligned_bytes_zeroed(
        ::foundation::effects::testing::test().alloc,
        ::foundation::alloc::mint_layout(core::ByteCount{64}, ::foundation::alloc::Alignment{64})
            .expect("the fixture gives a layout"));
    ::foundation::ByteWindow const empty{core::side_as<core::side::WindowStart>(core::ByteCount{0}),
                                         core::side_as<core::side::WindowLength>(core::ByteCount{0})};
    core::FixedArray<::foundation::ByteWindow, 9> const windows{empty, empty, empty, empty, empty,
                                                                empty, empty, empty, empty};
    auto done = ::foundation::with_mut_windows<float>(block, windows, [](auto...) noexcept {});
    return done.is_ok() ? 0 : 1;
}
