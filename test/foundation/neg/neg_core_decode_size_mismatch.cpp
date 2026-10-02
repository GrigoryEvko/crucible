// decode copies each byte of the source into the result, so the two types
// must have one size.  A source of two bytes cannot make a value of four.

#include <foundation/core/Scalar.h>

#include <cstdint>

int main() { return ::foundation::core::decode<std::uint32_t>(std::uint16_t{1}).is_some() ? 0 : 1; }
