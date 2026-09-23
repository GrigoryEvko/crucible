// A capability to block, built over a buffer. A forged capability skips
// the key and the context together. No constructor is trivial, so the
// type is not an implicit-lifetime type, and the mandate of
// std::start_lifetime_as refuses it.

#include <foundation/effects/Capability.h>

#include <bit>
#include <memory>

namespace fe = ::foundation::effects;

int main() {
    alignas(8) unsigned char storage[8]{};
    auto* forged = std::start_lifetime_as<fe::Capability<fe::Effect::Block, fe::Bg>>(storage);
    (void)forged;
    return 0;
}
