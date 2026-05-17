// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-AUDIT-D7 fixture: `mint_swmr_stage` via fixy:: alias rejects
// when the second (writer) parameter is not a SWMR writer handle.
//
// Violation: passes a bare int as the writer.  In
// `swmr_stage_from_endpoint_gate::compute()`, the short-circuit
// `!safety::extract::is_swmr_writer_v<writer>` returns false →
// `CtxFitsSwmrStageFromEndpoint` fails → requires-clause fires.
//
// Distinct rejection class from the non-consumer-endpoint fixture:
// this exercises the is_swmr_writer_v gate rather than the
// IsConsumerEndpoint gate.
//
// Expected diagnostic: "associated constraints are not satisfied"
// pointing at CtxFitsSwmrStageFromEndpoint /
// swmr_stage_from_endpoint_gate / is_swmr_writer.

#include <crucible/concurrent/PermissionedSnapshot.h>
#include <crucible/concurrent/PermissionedSpscChannel.h>
#include <crucible/effects/ExecCtx.h>
#include <crucible/fixy/Pipe.h>
#include <crucible/permissions/Permission.h>

#include <optional>
#include <utility>

namespace eff   = crucible::effects;
namespace fpipe = crucible::fixy::pipe;
namespace conc  = crucible::concurrent;
namespace saf   = crucible::safety;

struct InTag {};
struct SnapTag {};

using InChannel = conc::PermissionedSpscChannel<int, 64, InTag>;
using Snapshot  = conc::PermissionedSnapshot<int, SnapTag>;

inline void swmr_publish_body(InChannel::ConsumerHandle&&,
                              Snapshot::WriterHandle&&) noexcept {}

int main() {
    eff::HotFgCtx ctx;

    InChannel in;

    auto whole = saf::mint_permission_root<typename InChannel::whole_tag>();
    auto [prod_perm, cons_perm] = saf::mint_permission_split<
        typename InChannel::producer_tag,
        typename InChannel::consumer_tag>(std::move(whole));
    (void)prod_perm;

    auto cons = in.consumer(std::move(cons_perm));
    auto in_ep = fpipe::mint_endpoint<InChannel, fpipe::Direction::Consumer>(
        ctx, cons);

    int not_a_writer = 0;

    auto bad = fpipe::mint_swmr_stage<&swmr_publish_body>(
        ctx, std::move(in_ep), not_a_writer);
    (void)bad;
    return 0;
}
