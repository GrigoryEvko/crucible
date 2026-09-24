// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// An epoch wrapper over a background context, built with no context to
// wrap.  The wrapper is an execution context of its own, so a default
// wrapper admits every gate that admits Bg.  The wrapper has no default
// constructor.  The one route to a wrapper is the claim door, which
// reads the live epoch source.

#include <crucible/sessions/SessionMint.h>

namespace eff = ::crucible::effects;
namespace proto = ::crucible::safety::proto;

int main() {
    proto::EpochExecCtx<1, 1, eff::BgDrainCtx> forged{};
    (void)forged;
    return 0;
}
