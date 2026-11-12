// An Err marks a return, and it is not an error value.  A Result whose
// error is an Err would mark the mark again, so the error gate refuses each
// Err.

#include <foundation/core/Choice.h>

#include <cstdint>

enum class Code : std::uint8_t {
    full
};

int main() { return sizeof(::foundation::core::Result<int, ::foundation::core::Err<Code>>) > 0 ? 0 : 1; }
