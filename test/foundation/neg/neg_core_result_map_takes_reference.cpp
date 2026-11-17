// map gives up the Result, and its value ends when map returns.  A function
// that takes the value by reference names an object that ends, so map
// refuses it.

#include <foundation/core/Choice.h>

#include <cstdint>

enum class Code : std::uint8_t {
    closed
};

int main() {
    using Number = ::foundation::core::Result<int, Code>;
    ::foundation::core::Result<long, Code> const mapped =
        Number{4}.map([](int const& value) noexcept { return long{value}; });
    return mapped.is_ok() ? 0 : 1;
}
