// The key that mints every Capability, built from a byte. A forged key
// mints a capability with no context. Each constructor is user-provided,
// so the type is not trivially copyable, and std::bit_cast has no
// candidate.

#include <foundation/effects/Capability.h>

#include <bit>
#include <memory>

namespace fe = ::foundation::effects;

int main() {
    auto forged = std::bit_cast<fe::cap_mint_key>(char{0});
    (void)forged;
    return 0;
}
