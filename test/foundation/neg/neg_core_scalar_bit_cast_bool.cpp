// bit_cast takes only a To of which each bit pattern is a value: a bool from
// the byte 2 is no value, so bit_cast to bool does not compile.  decode is
// the checked copy.

#include <foundation/core/Scalar.h>

#include <cstdint>

int main() { return ::foundation::core::bit_cast<bool>(std::uint8_t{2}) ? 0 : 1; }
