// or_else gives up the Result, and its error ends when or_else returns.  A
// function that takes the error by reference names an object that ends, so
// or_else refuses it.

#include <foundation/core/Choice.h>

#include <cstdint>

enum class Code : std::uint8_t {
    closed
};

int main() {
    using Number = ::foundation::core::Result<int, Code>;
    Number const recovered =
        Number{::foundation::core::err(Code::closed)}.or_else([](Code const&) noexcept { return Number{0}; });
    return recovered.is_ok() ? 0 : 1;
}
