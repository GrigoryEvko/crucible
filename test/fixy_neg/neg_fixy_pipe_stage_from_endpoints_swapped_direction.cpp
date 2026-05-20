// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-U-074o fixture #2 for fixy::pipe::mint_stage_from_endpoints
// (StageEndpointBridge.h:510).  The requires-clause
// `CtxFitsStageFromEndpoints` demands `IsConsumerEndpoint<ConsumerEp>`
// for the in-slot and `IsProducerEndpoint<ProducerEp>` for the out-slot.
// Passing the Producer endpoint in the consumer position (and the
// Consumer endpoint in the producer position) inverts both — the
// IsConsumerEndpoint / IsProducerEndpoint conjuncts fire.
//
// (The substrate-side equivalents live in test/effects_neg/, which
// gen-mint-inventory does not count toward the fixy umbrella's HS14 floor.)
//
// Distinct mismatch class from
// neg_fixy_pipe_stage_from_endpoints_non_ctx.cpp (#1): there the ctx axis
// failed (non-ExecCtx) with correctly-paired endpoints; here the ctx is
// valid and the ENDPOINT DIRECTIONS are swapped.
//
// Expected diagnostic: associated constraints are not satisfied /
// CtxFitsStageFromEndpoints / IsConsumerEndpoint / IsProducerEndpoint.

#include <crucible/concurrent/Endpoint.h>
#include <crucible/concurrent/StageEndpointBridge.h>
#include <crucible/effects/ExecCtx.h>
#include <crucible/fixy/Pipe.h>

#include <utility>

namespace conc  = crucible::concurrent;
namespace eff   = crucible::effects;
namespace saf   = crucible::safety;
namespace fpipe = crucible::fixy::pipe;

namespace neg_fixy_pipe_stage_from_endpoints_swapped_direction {
struct UTag1 {};
struct UTag2 {};

using Ch1 = conc::PermissionedSpscChannel<int, 64, UTag1>;
using Ch2 = conc::PermissionedSpscChannel<int, 64, UTag2>;

inline void body(typename Ch1::ConsumerHandle&&,
                 typename Ch2::ProducerHandle&&) noexcept {}
}  // namespace neg_fixy_pipe_stage_from_endpoints_swapped_direction

int main() {
    namespace ns = neg_fixy_pipe_stage_from_endpoints_swapped_direction;
    eff::HotFgCtx ctx;

    ns::Ch1 ch1;
    ns::Ch2 ch2;

    auto w1 = saf::mint_permission_root<conc::spsc_tag::Whole<ns::UTag1>>();
    auto [pp1, cp1] = saf::mint_permission_split<
        conc::spsc_tag::Producer<ns::UTag1>,
        conc::spsc_tag::Consumer<ns::UTag1>>(std::move(w1));
    auto w2 = saf::mint_permission_root<conc::spsc_tag::Whole<ns::UTag2>>();
    auto [pp2, cp2] = saf::mint_permission_split<
        conc::spsc_tag::Producer<ns::UTag2>,
        conc::spsc_tag::Consumer<ns::UTag2>>(std::move(w2));

    auto cons1 = ch1.consumer(std::move(cp1));
    auto prod2 = ch2.producer(std::move(pp2));

    auto cons_ep = conc::mint_endpoint<ns::Ch1, conc::Direction::Consumer>(ctx, cons1);
    auto prod_ep = conc::mint_endpoint<ns::Ch2, conc::Direction::Producer>(ctx, prod2);

    // Swapped: producer endpoint in the consumer slot, consumer endpoint in
    // the producer slot.  IsConsumerEndpoint / IsProducerEndpoint fire.
    auto bad = fpipe::mint_stage_from_endpoints<&ns::body>(
        ctx, std::move(prod_ep), std::move(cons_ep));
    (void)bad;
    (void)pp1;
    (void)cp2;
    return 0;
}
