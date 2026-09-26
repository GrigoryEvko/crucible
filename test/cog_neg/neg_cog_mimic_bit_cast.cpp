// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// CogMimic is not trivially copyable, so std::bit_cast cannot build one
// from bytes that hold the address of an identity.  The private constructor
// alone would not stop that route.

#include <crucible/mimic/CogMimic.h>

#include <array>
#include <bit>
#include <cstddef>

namespace cog = crucible::cog;
namespace mimic = crucible::mimic;

int main() {
    const std::array<std::byte, sizeof(mimic::CogMimic<cog::CogKind::Gpu>)> image{};
    const auto forged = std::bit_cast<mimic::CogMimic<cog::CogKind::Gpu>>(image);
    (void)forged;
    return 0;
}
