// A body takes the prefix of a const ViewMut.  A const ViewMut writes
// nothing, so it gives no writer of a prefix.

#include <foundation/core/Array.h>

int main() {
    ::foundation::core::FixedArray<int, 4> array{1, 2, 3, 4};
    array.with_mut([](auto out) noexcept {
        auto const& shared = out;
        auto part = ::foundation::core::take_prefix_up_to(shared, 2);
        static_cast<void>(part);
    });
    return 0;
}
