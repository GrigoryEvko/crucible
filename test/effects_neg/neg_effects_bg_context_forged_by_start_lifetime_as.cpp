// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// A background capability, built over a buffer.  No constructor of Bg
// is trivial, so the type is not an implicit-lifetime type, and the
// mandate of std::start_lifetime_as refuses it.

#include <crucible/effects/_Capabilities.h>

#include <memory>

namespace eff = ::crucible::effects;

int main() {
    alignas(8) unsigned char storage[8]{};
    auto* forged = std::start_lifetime_as<eff::Bg>(storage);
    (void)forged;
    return 0;
}
