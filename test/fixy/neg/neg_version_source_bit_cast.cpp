// A version source is not rebuilt from bytes.  It has no copy and no move,
// so it is not trivially copyable, and std::bit_cast refuses it.  A second
// source made that way could stamp any version its bytes spell.

#include <fixy/EpochVersioned.h>

#include <array>
#include <bit>
#include <cstddef>

int main() {
    auto const source = std::bit_cast<fixy::VersionSource>(std::array<std::byte, sizeof(fixy::VersionSource)>{});
    return static_cast<int>(source.stamp().epoch().raw());
}
