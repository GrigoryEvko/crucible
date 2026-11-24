// Negative-compile test. This file must fail to compile.
//
// Each value of a match has one arm.  The two arms name acquire, so the
// value would have two bodies, and the build refuses the match.
//
// Expected diagnostic: the arms name one value two times, the enumerator
// acquire of Strength.

#include <foundation/core/EnumMatch.h>

#include <cstdint>

enum class Strength : std::uint8_t {
    none,
    acquire,
    release,
    seq_cst
};

int main() {
    namespace core = ::foundation::core;
    return core::match_enum(
        Strength::none, core::arm<Strength::none, Strength::acquire>([] noexcept { return 0; }),
        core::arm<Strength::acquire, Strength::release, Strength::seq_cst>([] noexcept { return 1; }));
}
