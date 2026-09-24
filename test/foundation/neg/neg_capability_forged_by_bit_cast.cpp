// A capability to block, built from a byte. A forged capability skips
// the key and the context together. Each constructor is user-provided,
// so the type is not trivially copyable, and std::bit_cast has no
// candidate.

#include <foundation/effects/Capability.h>

#include <bit>
#include <memory>

namespace fe = ::foundation::effects;

int main() {
    auto forged = std::bit_cast<fe::Capability<fe::Effect::Block, fe::Bg>>(char{0});
    (void)forged;
    return 0;
}
