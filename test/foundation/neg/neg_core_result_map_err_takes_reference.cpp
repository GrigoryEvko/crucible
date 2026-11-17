// map_err gives up the Result, and its error ends when map_err returns.  A
// function that takes the error by reference names an object that ends, so
// map_err refuses it.

#include <foundation/core/Choice.h>

#include <cstdint>

enum class Code : std::uint8_t {
    closed
};
enum class NetCode : std::uint8_t {
    refused
};

int main() {
    using Number = ::foundation::core::Result<int, Code>;
    ::foundation::core::Result<int, NetCode> const converted =
        Number{4}.map_err([](Code const&) noexcept { return NetCode::refused; });
    return converted.is_ok() ? 0 : 1;
}
