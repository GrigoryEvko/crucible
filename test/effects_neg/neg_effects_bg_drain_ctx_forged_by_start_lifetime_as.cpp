// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// A background drain context, built over a buffer.  The context holds a
// Bg, and cap() hands it out.  No constructor of the context is
// trivial, so the type is not an implicit-lifetime type, and the
// mandate of std::start_lifetime_as refuses it.

#include <crucible/effects/_ExecCtx.h>

#include <memory>

namespace eff = ::crucible::effects;

int main() {
    alignas(8) unsigned char storage[8]{};
    auto* forged = std::start_lifetime_as<eff::BgDrainCtx>(storage);
    (void)forged;
    return 0;
}
