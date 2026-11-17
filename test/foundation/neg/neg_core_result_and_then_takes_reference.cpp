// and_then gives up the Result, and its value ends when and_then returns.  A
// function that takes the value by reference names an object that ends, so
// and_then refuses it.

#include <foundation/core/Choice.h>

#include <cstdint>

enum class Code : std::uint8_t {
    closed
};

int main() {
    using Number = ::foundation::core::Result<int, Code>;
    Number const chained = Number{4}.and_then([](int const& value) noexcept { return Number{value}; });
    return chained.is_ok() ? 0 : 1;
}
