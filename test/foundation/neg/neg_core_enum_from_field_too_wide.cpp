// Negative-compile test. This file must fail to compile.
//
// Four enumerators fill a field of two bits, and no field of three bits: the
// values 4 to 7 name no enumerator.  enum_from_field refuses the width 3, so
// no read of a field gives a value that names no enumerator.
//
// Expected diagnostic: no enum_from_field of the width 3 takes the word, and
// the constraint FullField names the width.

#include <foundation/core/Scalar.h>

#include <cstdint>

enum class Phase : std::uint8_t {
    forward = 0,
    backward = 1,
    optimizer = 2,
    other = 3
};

int main() { return ::foundation::core::enum_from_field<Phase, 3>(std::uint8_t{5}) == Phase::other ? 0 : 1; }
