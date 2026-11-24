// A body gives the register runs of a const ViewMut and of a View to a body.
// A const ViewMut writes nothing, so it gives no writer of a run.

#include <foundation/Simd.h>
#include <foundation/core/Array.h>

#include <cstddef>

int main() {
    using Bytes = ::foundation::simd::vec<unsigned char, 16>;
    ::foundation::core::FixedArray<std::byte, 32> target{};
    ::foundation::core::FixedArray<std::byte, 32> const source{};
    target.with_mut([&source](auto run) noexcept {
        auto const& shared = run;
        auto rests = ::foundation::simd::for_each_register_run<Bytes>(
            shared, ::foundation::core::View<std::byte const>{source.view()}, [](auto out, auto in) noexcept {
                static_cast<void>(out);
                static_cast<void>(in);
            });
        static_cast<void>(rests);
    });
    return 0;
}
