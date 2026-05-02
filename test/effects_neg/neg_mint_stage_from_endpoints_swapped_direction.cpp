// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// #868 (CLAUDE.md §XXI HS14): mint_stage_from_endpoints requires
// IsConsumerEndpoint<ConsumerEp> ∧ IsProducerEndpoint<ProducerEp>.
// Passing a Producer-direction endpoint where a Consumer is expected
// (or vice versa) fires the IsConsumerEndpoint / IsProducerEndpoint
// conjuncts of CtxFitsStageFromEndpoints.
//
// Violation: passes the Producer endpoint as the consumer-side
// argument, and the Consumer endpoint as the producer-side argument
// (swapped).  Both endpoints are valid Endpoint specializations, but
// their Direction tags don't match what the bridge expects on each
// side.
//
// Expected diagnostic: "associated constraints are not satisfied"
// pointing at CtxFitsStageFromEndpoints / IsConsumerEndpoint /
// IsProducerEndpoint.

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

// Standard-shape PipelineStage body.
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
    auto prod2 = ch2.producer(std::move(pp2));

    auto cons_ep = conc::mint_endpoint<Ch1, conc::Direction::Consumer>(ctx, cons1);
    auto prod_ep = conc::mint_endpoint<Ch2, conc::Direction::Producer>(ctx, prod2);

    // Bridge fires: passes prod_ep where IsConsumerEndpoint expected,
    // cons_ep where IsProducerEndpoint expected.
    auto bad = conc::mint_stage_from_endpoints<&body>(
        ctx, std::move(prod_ep), std::move(cons_ep));
    (void)bad;
    (void)pp1;
    (void)cp2;
    return 0;
}
