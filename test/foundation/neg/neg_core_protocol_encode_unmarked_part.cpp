// The byte codec of the Protocol family refuses a part of a class with no
// annotation derive_protocol.  The class gives no consent to the walk of its
// state, so its bytes have no place in an image, also when it has ==.

#include <foundation/core/Protocol.h>

#include <cstddef>

struct Unmarked {
    unsigned word = 0;
    friend constexpr bool operator==(Unmarked, Unmarked) noexcept = default;
};

struct[[= ::foundation::core::derive_protocol{}]] HoldsUnmarked {
    Unmarked unmarked{};
};

int main() {
    HoldsUnmarked const value{};
    ::foundation::core::FixedArray<std::byte, 4> storage{};
    return storage.with_mut([&value](auto image) noexcept -> int {
        ::foundation::core::encode(value, image.view());
        return 0;
    });
}
