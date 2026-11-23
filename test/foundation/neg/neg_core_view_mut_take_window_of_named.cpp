// A body takes a window of a named ViewMut.  The window would write the
// elements of the run while the name stays with the body, so the run would
// have two writers.  take_window takes its ViewMut by move.

#include <foundation/core/Array.h>

int main() {
    ::foundation::core::FixedArray<int, 4> array{1, 2, 3, 4};
    array.with_mut([](auto out) noexcept {
        auto part = ::foundation::core::take_window(out, 1, 2);
        static_cast<void>(part);
    });
    return 0;
}
