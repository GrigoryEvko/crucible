// The key that mints every Capability, built over a buffer. A forged key
// mints a capability with no context. No constructor is trivial, so the
// type is not an implicit-lifetime type, and the mandate of
// std::start_lifetime_as refuses it.

#include <foundation/effects/Capability.h>

#include <bit>
#include <memory>

namespace fe = ::foundation::effects;

int main() {
    alignas(8) unsigned char storage[8]{};
    auto* forged = std::start_lifetime_as<fe::cap_mint_key>(storage);
    (void)forged;
    return 0;
}
