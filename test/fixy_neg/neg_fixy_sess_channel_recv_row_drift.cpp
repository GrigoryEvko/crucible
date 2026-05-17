// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-MINT-Sess fixture #2: mint_channel via fixy:: alias rejects
// when endpoint A's continuation Recv carries an IO row that
// endpoint A's HotFgCtx (Row<>) cannot admit.
//
// Routing through `fixy::sess::mint_channel` must reject identically
// to the substrate `proto::mint_channel` — proves the
// CtxFitsChannel gate surfaces through the alias.
//
// Expected diagnostic: CtxFitsChannel / CtxFitsPermissionedProtocol
// constraints not satisfied.

#include <crucible/effects/Computation.h>
#include <crucible/fixy/Sess.h>

namespace eff   = crucible::effects;
namespace fsess = crucible::fixy::sess;

namespace neg_fixy_sess_channel_recv_row_drift {
struct Payload {};
struct Resource {};
}  // namespace neg_fixy_sess_channel_recv_row_drift

int main() {
    namespace test_ns = neg_fixy_sess_channel_recv_row_drift;
    using IoPayload =
        eff::Computation<eff::Row<eff::Effect::IO>, test_ns::Payload>;
    using Proto = fsess::Send<int, fsess::Recv<IoPayload, fsess::End>>;

    eff::HotFgCtx endpoint_a;
    eff::BgCompileCtx endpoint_b;
    [[maybe_unused]] auto channel =
        fsess::mint_channel<Proto>(endpoint_a, endpoint_b,
                                   test_ns::Resource{}, test_ns::Resource{});
    return 0;
}
