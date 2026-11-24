// Negative-compile test. This file must fail to compile.
//
// A closed enumeration takes no arm otherwise.  Such an arm would take each
// enumerator that no arm names, also an enumerator that is added later, as
// the default arm of a switch does.
//
// Expected diagnostic: a closed enumeration takes no arm otherwise.

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
    return core::match_enum(Strength::none, core::arm<Strength::none>([] noexcept { return 0; }),
                            core::otherwise([](Strength) noexcept { return 1; }));
}
