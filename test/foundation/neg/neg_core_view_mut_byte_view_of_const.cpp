// A body takes the bytes of a const ViewMut.  A const ViewMut writes nothing,
// so it gives no writer of its bytes.

#include <foundation/core/Array.h>

int main() {
    ::foundation::core::FixedArray<unsigned char, 4> storage{};
    storage.with_mut([](auto run) noexcept {
        auto const& shared = run;
        auto bytes = ::foundation::core::byte_view(shared);
        static_cast<void>(bytes);
    });
    return 0;
}
