// An Err marks a return of a Result, and it is not a value.  An Option of
// an Err would drop the error with no trace, so the payload gate refuses
// each Err.

#include <foundation/core/Choice.h>

#include <cstdint>

enum class Code : std::uint8_t {
    full
};

int main() { return sizeof(::foundation::core::Option<::foundation::core::Err<Code>>) > 0 ? 0 : 1; }
