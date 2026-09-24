// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// An epoch wrapper over the foreground context, built with no epoch
// source.  A foreground context costs nothing to build, so a default
// wrapper claims an epoch that no source holds, and every mint that reads
// the epoch admits the claim.  The wrapper has no default constructor.
// The one route to a wrapper is the claim door, which reads the live
// epoch source.

#include <crucible/sessions/SessionMint.h>

namespace eff = ::crucible::effects;
namespace proto = ::crucible::safety::proto;

int main() {
    proto::EpochExecCtx<7, 3, eff::HotFgCtx> forged{};
    (void)forged;
    return 0;
}
