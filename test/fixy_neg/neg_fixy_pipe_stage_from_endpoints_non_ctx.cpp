// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// `concurrent::mint_stage_from_endpoints` rejects a context that is not an
// execution context.  The template parameter list of the factory
// constrains `::crucible::effects::IsExecCtx Ctx`.  A plain struct does not
// satisfy IsExecCtx, so the constraint rejects the call also with correct
// endpoints.  The endpoints here are correct: one consumer and one
// producer, made with a real HotFgCtx.  The context argument is the only
// cause of the rejection.
//
// The effects_neg fixture with swapped endpoint directions stops at a
// different check.  That fixture has a correct context, and this fixture
// has correct endpoints.
//
// Expected diagnostic: IsExecCtx, constraints not satisfied, or
// no matching function.

#include <crucible/concurrent/Endpoint.h>
#include <crucible/concurrent/StageEndpointBridge.h>
#include <crucible/effects/_ExecCtx.h>

#include <utility>

namespace conc = crucible::concurrent;
namespace eff = crucible::effects;
namespace saf = crucible::safety;

namespace neg_fixy_pipe_stage_from_endpoints_non_ctx {
struct UTag1 {};
struct UTag2 {};
struct NotAnExecCtx {};

using Ch1 = conc::PermissionedSpscChannel<int, 64, UTag1>;
using Ch2 = conc::PermissionedSpscChannel<int, 64, UTag2>;

inline void body(typename Ch1::ConsumerHandle&&, typename Ch2::ProducerHandle&&) noexcept {}
}  // namespace neg_fixy_pipe_stage_from_endpoints_non_ctx

int main() {
    namespace ns = neg_fixy_pipe_stage_from_endpoints_non_ctx;
    eff::HotFgCtx ctx;

    ns::Ch1 ch1;
    ns::Ch2 ch2;

    auto w1 = saf::mint_permission_root<conc::spsc_tag::Whole<ns::UTag1>>();
    auto [pp1, cp1] =
        saf::mint_permission_split<conc::spsc_tag::Producer<ns::UTag1>, conc::spsc_tag::Consumer<ns::UTag1>>(
            std::move(w1));
    auto w2 = saf::mint_permission_root<conc::spsc_tag::Whole<ns::UTag2>>();
    auto [pp2, cp2] =
        saf::mint_permission_split<conc::spsc_tag::Producer<ns::UTag2>, conc::spsc_tag::Consumer<ns::UTag2>>(
            std::move(w2));

    auto cons1 = ch1.consumer(std::move(cp1));
    auto prod2 = ch2.producer(std::move(pp2));

    // The endpoints are correct, because a real HotFgCtx makes them.
    auto cons_ep = conc::mint_endpoint<ns::Ch1, conc::Direction::Consumer>(ctx, cons1);
    auto prod_ep = conc::mint_endpoint<ns::Ch2, conc::Direction::Producer>(ctx, prod2);

    // NotAnExecCtx does not satisfy the IsExecCtx template constraint.
    auto bad = conc::mint_stage_from_endpoints<&ns::body>(ns::NotAnExecCtx{}, std::move(cons_ep), std::move(prod_ep));
    (void)bad;
    (void)pp1;
    (void)cp2;
    return 0;
}
