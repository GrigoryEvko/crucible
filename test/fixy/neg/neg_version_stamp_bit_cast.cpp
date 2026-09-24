// A stamp is not rebuilt from bytes.  Its copy is user-provided, so it is
// not trivially copyable, and std::bit_cast refuses it.

#include <fixy/EpochVersioned.h>

#include <array>
#include <bit>
#include <cstddef>

int main() {
    auto const stamp = std::bit_cast<fixy::VersionStamp>(std::array<std::byte, 16>{});
    return static_cast<int>(stamp.epoch().raw());
}
