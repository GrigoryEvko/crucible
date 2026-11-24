// Negative-compile test. This file must fail to compile.
//
// A match over a closed enumeration names each enumerator in an arm.  The
// enumerator seq_cst was added later, and no arm names it, so the build
// refuses the match until an arm takes it.
//
// Expected diagnostic: no arm names the enumerator seq_cst of Strength.

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
    return core::match_enum(Strength::none, core::arm<Strength::none, Strength::acquire>([] noexcept { return 0; }),
                            core::arm<Strength::release>([] noexcept { return 1; }));
}
