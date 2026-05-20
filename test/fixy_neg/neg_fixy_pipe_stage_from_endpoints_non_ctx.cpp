// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-U-074o fixture #1 for fixy::pipe::mint_stage_from_endpoints
// (StageEndpointBridge.h:510).  The factory's template parameter list
// constrains `::crucible::effects::IsExecCtx Ctx`; a plain struct that
// does NOT satisfy IsExecCtx fails that constraint regardless of how
// well-formed the endpoints are.  The endpoints here are VALID (one
// Consumer, one Producer, built with a real HotFgCtx) so the ONLY reason
// the call is rejected is the non-ExecCtx ctx argument.
//
// (The substrate-side equivalents live in test/effects_neg/, which
// gen-mint-inventory does not count toward the fixy umbrella's HS14 floor.)
//
// Distinct mismatch class from
// neg_fixy_pipe_stage_from_endpoints_swapped_direction.cpp (#2): there the
// ctx is valid and the ENDPOINT DIRECTIONS are swapped (endpoint-shape
// axis); here the endpoints are correctly paired and the CTX axis fails.
//
// Expected diagnostic: IsExecCtx / constraints not satisfied /
// no matching function.

#include <crucible/concurrent/Endpoint.h>
#include <crucible/concurrent/StageEndpointBridge.h>
#include <crucible/effects/ExecCtx.h>
#include <crucible/fixy/Pipe.h>

#include <utility>

namespace conc  = crucible::concurrent;
namespace eff   = crucible::effects;
namespace saf   = crucible::safety;
namespace fpipe = crucible::fixy::pipe;

namespace neg_fixy_pipe_stage_from_endpoints_non_ctx {
struct UTag1 {};
struct UTag2 {};
struct NotAnExecCtx {};

using Ch1 = conc::PermissionedSpscChannel<int, 64, UTag1>;
using Ch2 = conc::PermissionedSpscChannel<int, 64, UTag2>;

inline void body(typename Ch1::ConsumerHandle&&,
                 typename Ch2::ProducerHandle&&) noexcept {}
}  // namespace neg_fixy_pipe_stage_from_endpoints_non_ctx

int main() {
    namespace ns = neg_fixy_pipe_stage_from_endpoints_non_ctx;
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

    // Endpoints are VALID (built with a real HotFgCtx).
    auto cons_ep = conc::mint_endpoint<ns::Ch1, conc::Direction::Consumer>(ctx, cons1);
    auto prod_ep = conc::mint_endpoint<ns::Ch2, conc::Direction::Producer>(ctx, prod2);

    // Bad ctx: NotAnExecCtx fails the IsExecCtx Ctx template constraint.
    auto bad = fpipe::mint_stage_from_endpoints<&ns::body>(
        ns::NotAnExecCtx{}, std::move(cons_ep), std::move(prod_ep));
    (void)bad;
    (void)pp1;
    (void)cp2;
    return 0;
}
