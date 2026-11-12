// bit_cast takes only a To of which each bit pattern is a value: a value
// that names no enumerator is no value of an enumeration with enumerators,
// so bit_cast to it does not compile.

#include <foundation/core/Scalar.h>

#include <cstdint>

enum class Phase : std::uint8_t {
    first,
    second
};

int main() { return ::foundation::core::bit_cast<Phase>(std::uint8_t{7}) == Phase::first ? 0 : 1; }
