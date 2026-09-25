// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// `mint_swmr_stage` rejects a call when the second parameter (the writer)
// is not a SWMR writer handle.
//
// Violation: a bare int is the writer.  In
// `swmr_stage_from_endpoint_gate::compute()`, the check
// `!safety::extract::is_swmr_writer_v<writer>` returns false.  Then
// `CtxFitsSwmrStageFromEndpoint` is false, and the requires clause rejects
// the call.
//
// The non-consumer-endpoint fixture stops at a different check.  This
// fixture stops at the is_swmr_writer_v check, and that fixture stops at
// the IsConsumerEndpoint check.
//
// Expected diagnostic: "associated constraints are not satisfied"
// at CtxFitsSwmrStageFromEndpoint, swmr_stage_from_endpoint_gate or
// is_swmr_writer.

#include <crucible/concurrent/_Endpoint.h>
#include <crucible/concurrent/_PermissionedSnapshot.h>
#include <crucible/concurrent/_StageEndpointBridge.h>
#include <crucible/concurrent/_PermissionedSpscChannel.h>
#include <crucible/effects/_ExecCtx.h>
#include <crucible/permissions/_Permission.h>

#include <optional>
#include <utility>

namespace eff = crucible::effects;
namespace conc = crucible::concurrent;
namespace saf = crucible::safety;

struct InTag {};
struct SnapTag {};

using InChannel = conc::PermissionedSpscChannel<int, 64, InTag>;
using Snapshot = conc::PermissionedSnapshot<int, SnapTag>;

inline void swmr_publish_body(InChannel::ConsumerHandle&&, Snapshot::WriterHandle&&) noexcept {}

int main() {
    eff::HotFgCtx ctx;

    InChannel in;

    auto whole = saf::mint_permission_root<typename InChannel::whole_tag>();
    auto [prod_perm, cons_perm] =
        saf::mint_permission_split<typename InChannel::producer_tag, typename InChannel::consumer_tag>(
            std::move(whole));
    (void)prod_perm;

    auto cons = in.consumer(std::move(cons_perm));
    auto in_ep = conc::mint_endpoint<InChannel, conc::Direction::Consumer>(ctx, cons);

    int not_a_writer = 0;

    auto bad = conc::mint_swmr_stage<&swmr_publish_body>(ctx, std::move(in_ep), not_a_writer);
    (void)bad;
    return 0;
}
