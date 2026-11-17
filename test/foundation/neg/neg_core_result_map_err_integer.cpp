// The error of a Result is an error value, and a plain integer is refused
// because its meaning is not in its type.  map_err gives a new error, so it
// refuses a function that gives an integer.

#include <foundation/core/Choice.h>

#include <cstdint>

enum class Code : std::uint8_t {
    closed
};

int main() {
    using Number = ::foundation::core::Result<int, Code>;
    auto const converted = Number{4}.map_err([](Code) noexcept { return 5; });
    return converted.is_ok() ? 0 : 1;
}
