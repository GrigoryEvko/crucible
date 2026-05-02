// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// #868 (CLAUDE.md §XXI HS14): mint_stage_from_endpoints<auto FnPtr>(...)
// requires StageHandlesMatchEndpoints<FnPtr, ConsumerEp, ProducerEp>:
// the endpoint pair's handle_type aliases must equal the types FnPtr's
// PipelineStage signature declares.
//
// Violation: FnPtr expects FakeConsumer<int>&& on slot 0, but the
// ConsumerEp wraps a Channel<float>::ConsumerHandle (different
// payload type).  Without StageHandlesMatchEndpoints the substitution
// failure would only surface deep inside mint_stage_from_endpoints
// → mint_stage's internal binding.  With the gate, we get a clean
// CtxFitsStageFromEndpoints concept-failure diagnostic at the
// mint_stage_from_endpoints call site.
//
// Expected diagnostic: "associated constraints are not satisfied"
// pointing at CtxFitsStageFromEndpoints / StageHandlesMatchEndpoints.

#include <crucible/concurrent/Endpoint.h>
#include <crucible/concurrent/StageEndpointBridge.h>
#include <crucible/effects/ExecCtx.h>

#include <utility>

namespace conc = crucible::concurrent;
namespace eff  = crucible::effects;
namespace saf  = crucible::safety;

struct UTagInt   {};
struct UTagFloat {};

using ChInt   = conc::PermissionedSpscChannel<int,   64, UTagInt>;
using ChFloat = conc::PermissionedSpscChannel<float, 64, UTagFloat>;

// Stage body expects a Channel<int>::ConsumerHandle on slot 0.
inline void int_input_stage(typename ChInt::ConsumerHandle&&,
                            typename ChInt::ProducerHandle&&) noexcept {}

int main() {
    eff::HotFgCtx ctx;

    // Build a Channel<float>::ConsumerHandle — wrong payload type
    // for FnPtr's slot-0 expectation.
    ChFloat ch_float;
    auto wf = saf::mint_permission_root<conc::spsc_tag::Whole<UTagFloat>>();
    auto [ppf, cpf] = saf::mint_permission_split<
        conc::spsc_tag::Producer<UTagFloat>,
        conc::spsc_tag::Consumer<UTagFloat>>(std::move(wf));
    auto cons_float = ch_float.consumer(std::move(cpf));
    auto in_ep_wrong = conc::mint_endpoint<ChFloat, conc::Direction::Consumer>(ctx, cons_float);

    // Producer side correct (Channel<int>::ProducerHandle).
    ChInt ch_int;
    auto wi = saf::mint_permission_root<conc::spsc_tag::Whole<UTagInt>>();
    auto [ppi, cpi] = saf::mint_permission_split<
        conc::spsc_tag::Producer<UTagInt>,
        conc::spsc_tag::Consumer<UTagInt>>(std::move(wi));
    auto prod_int = ch_int.producer(std::move(ppi));
    auto out_ep   = conc::mint_endpoint<ChInt, conc::Direction::Producer>(ctx, prod_int);

    // Bridge fires: ConsumerEp's handle_type is Channel<float>::ConsumerHandle,
    // but FnPtr's slot 0 declares Channel<int>::ConsumerHandle.
    auto bad = conc::mint_stage_from_endpoints<&int_input_stage>(
        ctx, std::move(in_ep_wrong), std::move(out_ep));
    (void)bad;
    (void)ppf;
    (void)cpi;
    return 0;
}
