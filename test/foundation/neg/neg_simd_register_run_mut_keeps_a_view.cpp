// The body of the run of a frame of for_each_register_run captures by
// reference an Option of a View.  The door gives the rest of the first run
// back to the caller as its one writer, so a View of a register run that the
// body kept there would read a run that another name writes after the walk.
// The door refuses a body that can store a borrow outside the walk
// (StoresNoBorrowOutside of foundation/core/CaptureStore.h).

#include <foundation/Simd.h>
#include <foundation/core/Array.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Region.h>

#include <cstddef>
#include <utility>

int main() {
    using Bytes = ::foundation::simd::vec<unsigned char, 16>;
    ::foundation::core::FixedArray<std::byte, 32> const source{};
    auto [array, key] = ::foundation::core::mint_keyed_owner(::foundation::core::fixed_array_owner<std::byte, 32>);
    array.with_mut(key, [&source](auto out) noexcept {
        ::foundation::core::Option<::foundation::core::View<std::byte const>> kept{::foundation::core::none};
        auto rests = ::foundation::simd::for_each_register_run<Bytes>(
            std::move(out), ::foundation::core::View<std::byte const>{source.view()},
            [&kept](auto run, auto other) noexcept {
                static_cast<void>(run);
                static_cast<void>(other);
                static_cast<void>(kept.is_some());
            });
        static_cast<void>(rests);
    });
    return 0;
}
