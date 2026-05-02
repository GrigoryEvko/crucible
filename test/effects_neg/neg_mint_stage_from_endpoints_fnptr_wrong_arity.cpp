// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// AUDIT-ROUND-2 #1 of 3 (replacement for the lvalue_pass fixture
// whose premise was wrong — forwarding refs DO admit lvalue binds).
//
// #868 (CLAUDE.md §XXI): mint_stage_from_endpoints requires
// PipelineStage<FnPtr> — FnPtr must be a void function with EXACTLY
// two parameters (Consumer-shape handle, Producer-shape handle).
// A 3-arg function does not satisfy PipelineStage and the bridge
// must reject via the CtxFitsStage conjunct of CtxFitsStageFromEndpoints.
//
// This fixture proves the bridge ALSO catches FnPtr-shape mismatches
// (not just handle-type mismatches via StageHandlesMatchEndpoints).
// The CtxFitsStage conjunct of CtxFitsStageFromEndpoints fires
// independently of the StageHandlesMatchEndpoints conjunct.
//
// Violation: passes a 3-arg FnPtr (consumer, producer, extra) where
// PipelineStage requires arity 2.
//
// Expected diagnostic: "associated constraints are not satisfied"
// pointing at CtxFitsStageFromEndpoints / CtxFitsStage / PipelineStage.

#include <crucible/concurrent/Endpoint.h>
#include <crucible/concurrent/StageEndpointBridge.h>
#include <crucible/effects/ExecCtx.h>

#include <utility>

namespace conc = crucible::concurrent;
namespace eff  = crucible::effects;
namespace saf  = crucible::safety;

struct UTagIn  {};
struct UTagOut {};

using ChIn  = conc::PermissionedSpscChannel<int, 64, UTagIn>;
using ChOut = conc::PermissionedSpscChannel<int, 64, UTagOut>;

// 3-arg function — NOT a PipelineStage.  PipelineStage<&body3> is
// false.  Used as the bridge's FnPtr, this triggers the CtxFitsStage
// conjunct of CtxFitsStageFromEndpoints.
inline void body3(typename ChIn::ConsumerHandle&&,
                  typename ChOut::ProducerHandle&&,
                  int) noexcept {}

int main() {
    eff::HotFgCtx ctx;

    ChIn  ch_in;
    ChOut ch_out;

    auto wi = saf::mint_permission_root<conc::spsc_tag::Whole<UTagIn>>();
    auto [ppi, cpi] = saf::mint_permission_split<
        conc::spsc_tag::Producer<UTagIn>,
        conc::spsc_tag::Consumer<UTagIn>>(std::move(wi));

    auto wo = saf::mint_permission_root<conc::spsc_tag::Whole<UTagOut>>();
    auto [ppo, cpo] = saf::mint_permission_split<
        conc::spsc_tag::Producer<UTagOut>,
        conc::spsc_tag::Consumer<UTagOut>>(std::move(wo));

    auto cons_in  = ch_in.consumer(std::move(cpi));
    auto prod_out = ch_out.producer(std::move(ppo));

    auto cons_ep = conc::mint_endpoint<ChIn,  conc::Direction::Consumer>(ctx, cons_in);
    auto prod_ep = conc::mint_endpoint<ChOut, conc::Direction::Producer>(ctx, prod_out);

    // Bridge fires: PipelineStage<&body3> is false (3-arg, not 2-arg).
    // CtxFitsStage conjunct of CtxFitsStageFromEndpoints rejects.
    auto bad = conc::mint_stage_from_endpoints<&body3>(
        ctx, std::move(cons_ep), std::move(prod_ep));
    (void)bad;
    (void)ppi;
    (void)cpo;
    return 0;
}
