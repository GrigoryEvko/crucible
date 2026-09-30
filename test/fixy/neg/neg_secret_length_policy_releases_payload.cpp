// The LengthOnly policy releases the length of a classified container and
// never its payload.  A caller that asks that policy for the bytes gets a
// count, and the count does not convert to the bytes.

#include <fixy/Secret.h>

#include <array>
#include <cstdint>
#include <utility>

int main() {
    auto key = fixy::mint_secret<std::array<std::uint8_t, 16>>();
    std::array<std::uint8_t, 16> bytes = std::move(key).declassify<fixy::tags::secret_policy::LengthOnly>();
    return bytes[0];
}
