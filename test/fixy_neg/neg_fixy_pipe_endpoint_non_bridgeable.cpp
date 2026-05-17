// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-MINT-Pipe fixture #1: mint_endpoint via fixy:: alias rejects
// when (Substrate, Direction) pair has no default_proto_for /
// handle_for specialization.
//
// Violation: PermissionedSnapshot + Direction::Producer — Snapshot's
// directions are SwmrWriter / SwmrReader.  Routing through
// `fixy::pipe::mint_endpoint` must reject identically to the
// substrate `concurrent::mint_endpoint` — proves the alias preserves
// IsBridgeableDirection.
//
// Expected diagnostic: "associated constraints are not satisfied"
// pointing at IsBridgeableDirection.

#include <crucible/fixy/Pipe.h>

namespace eff   = crucible::effects;
namespace fpipe = crucible::fixy::pipe;
namespace conc  = crucible::concurrent;

struct UserTag {};

int main() {
    using SnapT = conc::PermissionedSnapshot<int, UserTag>;

    SnapT snap;
    typename SnapT::WriterHandle* fake_handle = nullptr;

    eff::HotFgCtx fg;
    // Snapshot + Producer → IsBridgeableDirection fails via fixy::pipe.
    auto bad = fpipe::mint_endpoint<SnapT, fpipe::Direction::Producer>(
        fg, *fake_handle);
    (void)bad;
    (void)snap;
    return 0;
}
