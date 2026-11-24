// A body gives the parts of a const ViewMut and of a View to a body.  A const
// ViewMut writes nothing, so it gives no writer of a part.

#include <foundation/core/Array.h>

int main() {
    ::foundation::core::FixedArray<int, 8> array{};
    ::foundation::core::FixedArray<int, 8> const source{};
    array.with_mut([&source](auto out) noexcept {
        auto const& shared = out;
        auto rests = ::foundation::core::for_each_chunk_pair<4>(
            shared, ::foundation::core::View<int const>{source.view()}, [](auto part, auto other) noexcept {
                static_cast<void>(part);
                static_cast<void>(other);
            });
        static_cast<void>(rests);
    });
    return 0;
}
