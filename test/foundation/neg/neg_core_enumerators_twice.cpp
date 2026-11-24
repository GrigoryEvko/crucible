// Negative-compile test. This file must fail to compile.
//
// A stated set names each enumerator one time.  The set below names acquire
// two times, so it is no set.
//
// Expected diagnostic: the check of make_enumerators_ refuses the two names
// of acquire, in the set of Strength.

#include <foundation/core/EnumMatch.h>

#include <cstdint>

enum class Strength : std::uint8_t {
    none,
    acquire
};

inline constexpr auto kTwice = ::foundation::core::enumerators<Strength::acquire, Strength::acquire>;

int main() { return static_cast<int>(kTwice.count); }
