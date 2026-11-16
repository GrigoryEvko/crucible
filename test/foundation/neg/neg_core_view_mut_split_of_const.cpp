// A body of a frame door splits a const ViewMut into chunks.  A const ViewMut
// writes nothing, so it gives no writer of a part, and the split refuses it.

#include <foundation/core/Array.h>

#include <utility>

int main() {
    ::foundation::core::FixedArray<int, 4> array{1, 2, 3, 4};
    array = ::foundation::core::with_mut(std::move(array), [](auto out) noexcept {
        auto const& shared = out;
        auto chunks = ::foundation::core::split_chunks(shared, 2);
        static_cast<void>(chunks);
    });
    return 0;
}
