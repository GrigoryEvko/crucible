// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// AUDIT-3 (CLAUDE.md §XXI HS14): mint_endpoint<Substr, Dir>(ctx, h)
// rejects a (Substrate, Direction) pair without a default_proto_for /
// handle_for specialization.  Snapshot has no Producer direction;
// SPSC has no SwmrWriter direction.
//
// Violation: PermissionedSnapshot + Direction::Producer.  Snapshot's
// directions are SwmrWriter / SwmrReader.  IsBridgeableDirection
// fails.
//
// Expected diagnostic: "associated constraints are not satisfied"
// pointing at IsBridgeableDirection.

#include <crucible/concurrent/Endpoint.h>

namespace eff  = crucible::effects;
namespace conc = crucible::concurrent;
namespace saf  = crucible::safety;

struct UserTag {};

int main() {
    using SnapT = conc::PermissionedSnapshot<int, UserTag>;

    SnapT snap;
    // Snapshot's writer-side handle is mint via .writer(...) — the
    // mismatch we're testing is on the (Substrate, Direction) pair, so
    // we don't actually need to construct a real handle; the requires-
    // clause fires before any handle constraint matters.
    typename SnapT::WriterHandle* fake_handle = nullptr;

    eff::HotFgCtx fg;
    // Snapshot + Producer → IsBridgeableDirection fails.
    auto bad = conc::mint_endpoint<SnapT, conc::Direction::Producer>(
        fg, *fake_handle);
    (void)bad;
    (void)snap;
    return 0;
}
