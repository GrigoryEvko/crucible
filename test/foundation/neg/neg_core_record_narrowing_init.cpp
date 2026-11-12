// A Record is an aggregate, so its initialization is a list
// initialization, and a narrowing conversion does not compile.

#include <foundation/core/Record.h>

#include <cstdint>

int main() {
    int const wide = 300;
    ::foundation::core::Record<std::uint8_t, int> const narrowed{wide, 1};
    auto const& [first, second] = narrowed;
    return first + second;
}
