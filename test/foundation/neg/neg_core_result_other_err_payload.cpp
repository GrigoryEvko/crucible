// An Err marks a return, and it is not a value.  A Result whose value is an
// Err of a different error type would hold the mark of one error as the
// value of the other, so the payload gate refuses each Err.

#include <foundation/core/Choice.h>

#include <cstdint>

enum class Code : std::uint8_t {
    full
};

enum class Other : std::uint8_t {
    closed
};

int main() { return sizeof(::foundation::core::Result<::foundation::core::Err<Other>, Code>) > 0 ? 0 : 1; }
