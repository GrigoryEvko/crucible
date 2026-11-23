// A body takes the bytes of a named ViewMut.  The bytes would write the run
// while the name stays with the body, so the run would have two writers.
// The bytes of a ViewMut take it by move.

#include <foundation/core/Array.h>

int main() {
    ::foundation::core::FixedArray<unsigned char, 4> storage{};
    storage.with_mut([](auto run) noexcept {
        auto bytes = ::foundation::core::byte_view(run);
        static_cast<void>(bytes);
    });
    return 0;
}
