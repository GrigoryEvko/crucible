// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// An epoch wrapper over the foreground context, started over a buffer by the
// checked lifetime start.  The wrapper has no trivial constructor, so it is
// not an implicit-lifetime type, and the checked start refuses it.

#include <crucible/sessions/_SessionMint.h>
#include <foundation/Lifetime.h>

namespace eff = ::crucible::effects;
namespace proto = ::crucible::safety::proto;

int main() {
    alignas(8) unsigned char storage[8]{};
    auto forged = ::foundation::lifetime::start_as_array<proto::EpochExecCtx<9, 9, eff::HotFgCtx>>(storage, 1);
    (void)forged;
    return 0;
}
