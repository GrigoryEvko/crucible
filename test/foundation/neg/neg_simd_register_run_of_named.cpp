// A body gives the register runs of a named ViewMut and of a View to a body.
// The runs would write the run of the frame while the name stays with the
// body, so the run would have two writers.  for_each_register_run takes its
// ViewMut by move.

#include <foundation/Simd.h>
#include <foundation/core/Array.h>

#include <cstddef>

int main() {
    using Bytes = ::foundation::simd::vec<unsigned char, 16>;
    auto [target, target_key] =
        ::foundation::core::mint_keyed_owner(::foundation::core::fixed_array_owner<std::byte, 32>);
    ::foundation::core::FixedArray<std::byte, 32> const source{};
    target.with_mut(target_key, [&source](auto run) noexcept {
        auto rests = ::foundation::simd::for_each_register_run<Bytes>(
            run, ::foundation::core::View<std::byte const>{source.view()}, [](auto out, auto in) noexcept {
                static_cast<void>(out);
                static_cast<void>(in);
            });
        static_cast<void>(rests);
    });
    return 0;
}
