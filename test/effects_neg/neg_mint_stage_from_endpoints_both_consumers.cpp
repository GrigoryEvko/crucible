// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// AUDIT-ROUND-2 #2 of 3.
//
// #868 (CLAUDE.md §XXI): mint_stage_from_endpoints requires the
// PRODUCER-side parameter to be an Endpoint with Direction::Producer.
// Two consumer-direction endpoints (no producer) is invalid by
// shape — Stage's PipelineStage signature is fundamentally
// (consumer_in, producer_out), and a stage that consumes from two
// queues without producing anywhere makes no sense in pipeline
// composition.
//
// Distinct from neg_mint_stage_from_endpoints_swapped_direction
// (which proves direction PAIRING enforcement): this fixture proves
// that even WITHOUT swap, the bridge rejects two-consumer or
// two-producer pairs.  The IsProducerEndpoint conjunct fires on the
// second-position consumer.
//
// Violation: passes two Consumer-direction endpoints — the producer
// slot's IsProducerEndpoint check fires.
//
// Expected diagnostic: "associated constraints are not satisfied"
// pointing at CtxFitsStageFromEndpoints / IsProducerEndpoint.

#include <crucible/concurrent/Endpoint.h>
#include <crucible/concurrent/StageEndpointBridge.h>
#include <crucible/effects/ExecCtx.h>

#include <utility>

namespace conc = crucible::concurrent;
namespace eff  = crucible::effects;
namespace saf  = crucible::safety;

struct UTag1 {};
struct UTag2 {};

using Ch1 = conc::PermissionedSpscChannel<int, 64, UTag1>;
using Ch2 = conc::PermissionedSpscChannel<int, 64, UTag2>;

// Standard-shape PipelineStage body — does NOT match the
// (Consumer, Consumer) endpoint pair we're about to construct, but
// the bridge rejects on direction shape BEFORE handle-type matching
// because IsProducerEndpoint is a separate conjunct.
inline void body(typename Ch1::ConsumerHandle&&,
                 typename Ch2::ProducerHandle&&) noexcept {}

int main() {
    eff::HotFgCtx ctx;

    Ch1 ch1;
    Ch2 ch2;

    auto w1 = saf::mint_permission_root<conc::spsc_tag::Whole<UTag1>>();
    auto [pp1, cp1] = saf::mint_permission_split<
        conc::spsc_tag::Producer<UTag1>,
        conc::spsc_tag::Consumer<UTag1>>(std::move(w1));

    auto w2 = saf::mint_permission_root<conc::spsc_tag::Whole<UTag2>>();
    auto [pp2, cp2] = saf::mint_permission_split<
        conc::spsc_tag::Producer<UTag2>,
        conc::spsc_tag::Consumer<UTag2>>(std::move(w2));

    auto cons1 = ch1.consumer(std::move(cp1));
    auto cons2 = ch2.consumer(std::move(cp2));

    // Both consumer-direction endpoints.
    auto cons_ep1 = conc::mint_endpoint<Ch1, conc::Direction::Consumer>(ctx, cons1);
    auto cons_ep2 = conc::mint_endpoint<Ch2, conc::Direction::Consumer>(ctx, cons2);

    // Bridge fires: cons_ep2 passed where IsProducerEndpoint expected.
    auto bad = conc::mint_stage_from_endpoints<&body>(
        ctx, std::move(cons_ep1), std::move(cons_ep2));
    (void)bad;
    (void)pp1;
    (void)pp2;
    return 0;
}
