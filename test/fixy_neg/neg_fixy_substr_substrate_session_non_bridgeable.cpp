// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Substrate session mint fixture 1 of 2 for
// concurrent::mint_substrate_session: rejects when the (Substrate,
// Direction) pair is not bridgeable.
//
// PermissionedSnapshot's only directions are SwmrWriter / SwmrReader
// (see effects_neg/neg_endpoint_non_bridgeable_direction.cpp).  Requesting
// Direction::Producer has no default_proto_for / handle_for
// specialization, so the requires-clause conjunct
// IsBridgeableDirection<Snap, Producer> is false.
//
// Distinct mismatch class from
// neg_fixy_substr_substrate_session_non_ctx.cpp (#2): that supplies a
// bridgeable (Snap, SwmrWriter) but a non-ExecCtx first arg (fails the
// `::crucible::effects::IsExecCtx Ctx` template-parameter constraint);
// this supplies a valid HotFgCtx but a non-bridgeable direction (fails
// IsBridgeableDirection).  Two distinct rejection conjuncts ⇒ HS14 ≥2.
//
// Expected diagnostic: IsBridgeableDirection / constraints not
// satisfied.

#include <crucible/concurrent/PermissionedSnapshot.h>
#include <crucible/concurrent/SubstrateSessionBridge.h>
#include <crucible/effects/_ExecCtx.h>

namespace conc = crucible::concurrent;
namespace eff = crucible::effects;

namespace neg_fixy_substr_substrate_session_non_bridgeable {
struct UserTag {};
}  // namespace neg_fixy_substr_substrate_session_non_bridgeable

int main() {
    using Snap = conc::PermissionedSnapshot<int, neg_fixy_substr_substrate_session_non_bridgeable::UserTag>;

    // Compile-time rejection precedes any deref of the null handle.
    typename Snap::WriterHandle* fake_handle = nullptr;
    eff::HotFgCtx fg;

    // Snapshot + Producer → IsBridgeableDirection<Snap, Producer> false.
    [[maybe_unused]] auto bad = conc::mint_substrate_session<Snap, conc::Direction::Producer>(fg, *fake_handle);
    return 0;
}
