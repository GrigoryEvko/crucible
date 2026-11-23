// A body gives the parts of a named ViewMut to a body.  The parts would write
// the run while the name stays with the body, so the run would have two
// writers.  for_each_chunk takes its ViewMut by move.

#include <foundation/core/Array.h>

int main() {
    ::foundation::core::FixedArray<int, 8> array{};
    array.with_mut([](auto out) noexcept {
        auto rest = ::foundation::core::for_each_chunk<4>(out, [](auto part) noexcept { static_cast<void>(part); });
        static_cast<void>(rest);
    });
    return 0;
}
