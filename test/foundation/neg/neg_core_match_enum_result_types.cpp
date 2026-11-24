// Negative-compile test. This file must fail to compile.
//
// The match gives the result of the arm that runs, so each arm gives one
// result type.  The second arm gives a long, and the first an int.
//
// Expected diagnostic: each arm gives one result type.

#include <foundation/core/EnumMatch.h>

#include <cstdint>

enum class Strength : std::uint8_t {
    none,
    acquire
};

int main() {
    namespace core = ::foundation::core;
    return core::match_enum(Strength::none, core::arm<Strength::none>([] noexcept { return 0; }),
                            core::arm<Strength::acquire>([] noexcept { return 1L; }));
}
