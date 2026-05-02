// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// AUDIT-ROUND-2 #3 of 3.
//
// #868 (CLAUDE.md §XXI): mint_stage_from_endpoints requires
// IsConsumerEndpoint<ConsumerEp> ∧ IsProducerEndpoint<ProducerEp>
// — both arguments must be Endpoint<...> specializations, NOT
// raw handles.
//
// Crucially: a raw producer/consumer handle satisfies IsProducerHandle
// / IsConsumerHandle (FOUND-D05/D06) and would compose fine with
// mint_stage<FnPtr> directly (which is the deliberate-direct route).
// But the BRIDGE specifically requires Endpoint shape — without this
// gate, a user could accidentally bypass mint_endpoint's
// substrate-fit validation by handing raw handles to the bridge.
//
// This fixture proves the bridge's IsConsumerEndpoint / IsProducerEndpoint
// gates fire SPECIFICALLY on the Endpoint-vs-raw-handle distinction.
//
// Violation: passes a raw ConsumerHandle (which IS a valid Tier 2
// substrate handle, satisfying IsConsumerHandle) where the bridge
// expects an Endpoint specialization.  The IsConsumerEndpoint
// recognizer (specialization-based, not concept-based) rejects.
//
// Expected diagnostic: "associated constraints are not satisfied"
// pointing at CtxFitsStageFromEndpoints / IsConsumerEndpoint.

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

inline void body(typename ChIn::ConsumerHandle&&,
                 typename ChOut::ProducerHandle&&) noexcept {}

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

    // RAW handles, NOT wrapped via mint_endpoint.  Both satisfy
    // IsConsumerHandle / IsProducerHandle (FOUND-D05/D06), but
    // neither is an Endpoint specialization.
    auto raw_cons = ch_in.consumer(std::move(cpi));
    auto raw_prod = ch_out.producer(std::move(ppo));

    // Bridge fires: raw_cons is a Channel::ConsumerHandle, NOT an
    // Endpoint<ChIn, Direction::Consumer, HotFgCtx>.  The bridge's
    // IsConsumerEndpoint conjunct rejects.  Critically, this proves
    // the bridge enforces Endpoint shape and prevents users from
    // accidentally bypassing mint_endpoint's substrate-fit validation
    // by routing raw handles through the bridge.
    auto bad = conc::mint_stage_from_endpoints<&body>(
        ctx, std::move(raw_cons), std::move(raw_prod));
    (void)bad;
    (void)ppi;
    (void)cpo;
    return 0;
}
