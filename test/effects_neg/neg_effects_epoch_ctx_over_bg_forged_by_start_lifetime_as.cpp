// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// An epoch wrapper over a background context, built over a buffer.  An
// aggregate is an implicit-lifetime type, so the wrapper is not an
// aggregate, and no constructor of it is trivial.  The mandate of
// std::start_lifetime_as refuses it.

#include <crucible/sessions/_SessionMint.h>

#include <memory>

namespace eff = ::crucible::effects;
namespace proto = ::crucible::safety::proto;

int main() {
    alignas(8) unsigned char storage[8]{};
    auto* forged = std::start_lifetime_as<proto::EpochExecCtx<1, 1, eff::BgDrainCtx>>(storage);
    (void)forged;
    return 0;
}
