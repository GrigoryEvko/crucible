// Negative-compile test. This file must fail to compile.
//
// A bool is no word of bits: its one bit is a truth value, and a read of a
// field from it hides the compare that made it.  enum_from_field refuses a
// bool word.
//
// Expected diagnostic: no enum_from_field takes a bool, and the constraint
// names the type of the word.

#include <foundation/core/Scalar.h>

#include <cstdint>

enum class Side : std::uint8_t {
    left = 0,
    right = 1
};

int main() {
    bool const is_right = true;
    return ::foundation::core::enum_from_field<Side, 1>(is_right) == Side::right ? 0 : 1;
}
