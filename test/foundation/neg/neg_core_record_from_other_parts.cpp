// A Record has no converting constructor, so a Record of other parts never
// becomes this Record, also when each part converts.

#include <foundation/core/Record.h>

#include <cstdint>

int main() {
    ::foundation::core::Record<std::uint8_t, std::uint8_t> const small{1, 2};
    ::foundation::core::Record<int, int> const wide = small;
    auto const& [first, second] = wide;
    return first + second;
}
