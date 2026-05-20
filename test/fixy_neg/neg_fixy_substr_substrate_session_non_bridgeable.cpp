// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-U-074d HS14 fixture #1 for fixy::substr::mint_substrate_session:
// rejects when the (Substrate, Direction) pair is not bridgeable.
//
// PermissionedSnapshot's only directions are SwmrWriter / SwmrReader
// (see neg_fixy_pipe_endpoint_non_bridgeable.cpp).  Requesting
// Direction::Producer has no default_proto_for / handle_for
// specialization, so the requires-clause conjunct
// IsBridgeableDirection<Snap, Producer> is false.  Routing through
// `fixy::substr::mint_substrate_session` must reject identically to
// the substrate `concurrent::mint_substrate_session`.
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
#include <crucible/effects/ExecCtx.h>
#include <crucible/fixy/Substr.h>

namespace fsubstr = crucible::fixy::substr;
namespace conc    = crucible::concurrent;
namespace eff     = crucible::effects;

namespace neg_fixy_substr_substrate_session_non_bridgeable {
struct UserTag {};
}  // namespace neg_fixy_substr_substrate_session_non_bridgeable

int main() {
    using Snap = conc::PermissionedSnapshot<int,
        neg_fixy_substr_substrate_session_non_bridgeable::UserTag>;

    // Compile-time rejection precedes any deref of the null handle.
    typename Snap::WriterHandle* fake_handle = nullptr;
    eff::HotFgCtx fg;

    // Snapshot + Producer → IsBridgeableDirection<Snap, Producer> false.
    [[maybe_unused]] auto bad =
        fsubstr::mint_substrate_session<Snap, conc::Direction::Producer>(
            fg, *fake_handle);
    return 0;
}
