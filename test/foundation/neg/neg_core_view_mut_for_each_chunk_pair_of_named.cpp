// A body gives the parts of a named ViewMut and of a View to a body.  The
// parts would write the run while the name stays with the body, so the run
// would have two writers.  for_each_chunk_pair takes its ViewMut by move.

#include <foundation/core/Array.h>

int main() {
    ::foundation::core::FixedArray<int, 8> array{};
    ::foundation::core::FixedArray<int, 8> const source{};
    array.with_mut([&source](auto out) noexcept {
        auto rests = ::foundation::core::for_each_chunk_pair<4>(out, ::foundation::core::View<int const>{source.view()},
                                                                [](auto part, auto other) noexcept {
                                                                    static_cast<void>(part);
                                                                    static_cast<void>(other);
                                                                });
        static_cast<void>(rests);
    });
    return 0;
}
