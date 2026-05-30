// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture for fix-03 (§XXI Universal Mint Pattern compliance).
//
// Premise: SwmrStage<FnPtr, Ctx> exists ONLY to be the load-bearing
// product of mint_swmr_stage<FnPtr>(ctx, in_ep, writer).  A
// CRUCIBLE_ROW_MISMATCH_ASSERT inside mint_swmr_stage gates the
// swmr_stage_row_union vs ctx_row admission BEFORE construction.  A
// production-side `SwmrStage<&body, MyCtx>{ctx, in, writer}` direct
// construction would emit a SwmrStage whose effect rows never pass
// that check — the §XXI bypass fix-03 closes.
//
// Fix shape (fix-03): SwmrStage's previously-public ctor
// `SwmrStage(Ctx const&, consumer_handle_type&&, writer_handle_type&&)`
// moved to `private:`, and detail::make_swmr_stage friended so the only
// authorized authorization point remains the mint factory (which routes
// through that friend after running the row admission).  Any direct
// construction site is now ill-formed.
//
// Sibling fixtures (same closure pattern): neg_stage_ctor_public_bypass.cpp,
// neg_mpmc_stage_ctor_public_bypass.cpp.
//
// This fixture materializes the real handles mint_swmr_stage would consume
// (a SPSC consumer handle + a PermissionedSnapshot writer handle — fakes
// would fail CtxFitsSwmrPublishStage and the type would be unnameable) and
// attempts the rejected direct construction.  Build MUST fail; diagnostic
// MUST contain "private".

#include <crucible/concurrent/PermissionedSnapshot.h>
#include <crucible/concurrent/PermissionedSpscChannel.h>
#include <crucible/concurrent/Stage.h>
#include <crucible/effects/ExecCtx.h>
#include <crucible/permissions/Permission.h>

#include <utility>

namespace conc = crucible::concurrent;
namespace eff  = crucible::effects;
namespace saf  = crucible::safety;

struct InTag {};
struct SnapTag {};

using InChannel = conc::PermissionedSpscChannel<int, 64, InTag>;
using Snapshot  = conc::PermissionedSnapshot<int, SnapTag>;

inline void swmr_publish_body(InChannel::ConsumerHandle&&,
                              Snapshot::WriterHandle&&) noexcept {}

int main() {
    eff::HotFgCtx ctx;

    // Real consumer handle (the SwmrStage's input handle type).
    InChannel in;
    auto in_whole = saf::mint_permission_root<typename InChannel::whole_tag>();
    auto [prod_perm, cons_perm] = saf::mint_permission_split<
        typename InChannel::producer_tag,
        typename InChannel::consumer_tag>(std::move(in_whole));
    (void)prod_perm;
    auto cons = in.consumer(std::move(cons_perm));

    // Real SWMR writer handle (the SwmrStage's writer handle type).
    Snapshot snap;
    auto writer_perm = saf::mint_permission_root<typename Snapshot::writer_tag>();
    auto writer = snap.writer(std::move(writer_perm));

    // ── The §XXI bypass attempt ────────────────────────────────────
    //
    // Direct construction of SwmrStage<&swmr_publish_body, HotFgCtx>
    // bypasses mint_swmr_stage and therefore bypasses its row admission.
    // With the fix-03 fix, the ctor is private and this line is
    // ill-formed.  Before fix-03 this would have compiled silently.
    conc::SwmrStage<&swmr_publish_body, eff::HotFgCtx> stage{
        ctx, std::move(cons), std::move(writer)};
    (void)stage;

    return 0;
}
