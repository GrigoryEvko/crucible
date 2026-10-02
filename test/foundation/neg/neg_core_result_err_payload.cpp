// A Result converts from its value and from an Err of its error.  A value
// that is that Err would make the two conversions meet, so a Result of an
// Err is refused.

#include <foundation/core/Choice.h>

#include <cstdint>

enum class Code : std::uint8_t {
    full
};

int main() { return sizeof(::foundation::core::Result<::foundation::core::Err<Code>, Code>) > 0 ? 0 : 1; }
