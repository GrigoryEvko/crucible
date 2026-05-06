// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-086: mint_mpmc_stage_from_endpoints infers the input/output
// endpoint split from StageArity<FnPtr>.  The endpoint count must match
// the full variadic stage signature exactly.
//
// Violation: fan_in_body expects three consumer endpoints and one
// producer endpoint, but the call supplies only two consumers and one
// producer.  CtxFitsMpmcStageFromEndpoints must reject before any
// endpoint handle extraction occurs.

#include <crucible/concurrent/Endpoint.h>
#include <crucible/concurrent/StageEndpointBridge.h>
#include <crucible/effects/ExecCtx.h>

#include <utility>

namespace conc = crucible::concurrent;
namespace eff = crucible::effects;

struct InTag {};
struct OutTag {};

using In = conc::PermissionedSpscChannel<int, 64, InTag>;
using Out = conc::PermissionedSpscChannel<int, 64, OutTag>;
using ConsEp = conc::Endpoint<In, conc::Direction::Consumer, eff::HotFgCtx>;
using ProdEp = conc::Endpoint<Out, conc::Direction::Producer, eff::HotFgCtx>;

inline void fan_in_body(In::ConsumerHandle&&,
                        In::ConsumerHandle&&,
                        In::ConsumerHandle&&,
                        Out::ProducerHandle&&) noexcept {}

using Bad = decltype(conc::mint_mpmc_stage_from_endpoints<&fan_in_body>(
    std::declval<eff::HotFgCtx const&>(),
    std::declval<ConsEp&&>(),
    std::declval<ConsEp&&>(),
    std::declval<ProdEp&&>()));

int main() { return 0; }
