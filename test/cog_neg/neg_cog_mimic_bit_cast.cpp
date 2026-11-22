// Negative compile test. This file must fail to compile.
//
// CogMimic is not trivially copyable, so std::bit_cast cannot build one
// from bytes that hold the address of an identity.  The private constructor
// alone would not stop that route.

#include <crucible/mimic/CogMimic.h>
#include <fixy/Core.h>
#include <foundation/Quarantine.h>

#include <bit>
#include <cstddef>

namespace cog = crucible::cog;
namespace mimic = crucible::mimic;

int main() {
    const ::fixy::FixedArray<std::byte, sizeof(mimic::CogMimic<cog::CogKind::Gpu>)> image{};
    CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture forges an object from bytes with std::bit_cast")
    const auto forged = std::bit_cast<mimic::CogMimic<cog::CogKind::Gpu>>(image);
    (void)forged;
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
    return 0;
}
