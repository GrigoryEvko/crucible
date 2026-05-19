// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-HS14-03 negative fixture #2 (HS14 ≥2 floor):
// mint_recording_endpoint second-parameter reference-binding route
// via the `fixy::bridge::` re-export — distinct mismatch class from
// fixture #1 (non_endpoint / first-param template-deduction).
//
// `mint_recording_endpoint(ep, log, self, peer)` wraps an Endpoint's
// bare session view in a RecordingSessionHandle.  The second
// parameter is `SessionEventLog&`.  After the first parameter binds
// successfully (the inbound argument IS a valid Endpoint via
// `fpipe::mint_endpoint`), the second parameter's reference type
// fails to bind against a non-SessionEventLog argument.  The
// diagnostic specifically names the SessionEventLog requirement
// rather than the blanket "no overload matches" / "could not
// deduce" produced by fixture #1's first-parameter template-
// deduction rejection.
//
// Why this is a DISTINCT mismatch class from fixture #1:
//   - Fixture #1 (non_endpoint): first-parameter template-
//     deduction.  `mint_recording_endpoint` is a function template
//     whose first parameter is `Endpoint<Substr, Dir, Ctx>&&` —
//     passing `int` fails template-argument deduction, producing a
//     "no matching function for call to" / "could not deduce"
//     diagnostic against the whole template.
//   - Fixture #2 (wrong_log_type, this file): first-parameter
//     SUCCESSFULLY DEDUCES (the inbound IS a real Endpoint
//     constructed via `fpipe::mint_endpoint<Channel,
//     Direction::Consumer>(ctx, cons)`) — Substr / Dir / Ctx all
//     resolve.  The failure is post-deduction reference-binding on
//     the SECOND parameter: `SessionEventLog&` cannot bind to `int`.
//     Diagnostic: "cannot bind non-const lvalue reference of type
//     'SessionEventLog&'" / "could not convert".
//
// This fixture is GENUINELY more rigorous than the substrate-side
// equivalent (which ships a `FakeEndpoint` and still trips the
// first-param gate, mis-labelled as "wrong_log_type").  Here the
// first param is a real Endpoint and the failure is unambiguously
// on the second.
//
// The `fixy::bridge::` re-export must reject identically — the
// using-decl preserves the exact parameter shape, not just the
// requires-clause.  A regression where the using-decl accidentally
// exposed a wider function-template forwarder with weaker
// constraints would let the wrong-log-type call compile at the
// fixy:: layer while the substrate-side equivalent still rejected.
// This fixture closes that gap.
//
// Expected diagnostic: "no matching function for call to
// 'mint_recording_endpoint'" OR "cannot bind non-const lvalue
// reference of type 'SessionEventLog&'" OR "could not convert".

#include <crucible/concurrent/PermissionedSpscChannel.h>
#include <crucible/effects/ExecCtx.h>
#include <crucible/fixy/Bridge.h>
#include <crucible/fixy/Pipe.h>
#include <crucible/permissions/Permission.h>
#include <crucible/sessions/SessionEventLog.h>

#include <utility>

namespace eff     = ::crucible::effects;
namespace conc    = ::crucible::concurrent;
namespace fbridge = ::crucible::fixy::bridge;
namespace fpipe   = ::crucible::fixy::pipe;
namespace proto   = ::crucible::safety::proto;
namespace saf     = ::crucible::safety;

struct EndpointTag {};

using Channel = conc::PermissionedSpscChannel<int, 64, EndpointTag>;

int main() {
    eff::HotFgCtx ctx;

    Channel ch;

    // Build a valid consumer Endpoint.  The first-param gate
    // (template-deduction + IsBridgeableDirection) passes; the
    // failure is on the second parameter.
    auto whole = saf::mint_permission_root<typename Channel::whole_tag>();
    auto [prod_perm, cons_perm] = saf::mint_permission_split<
        typename Channel::producer_tag,
        typename Channel::consumer_tag>(std::move(whole));
    (void)prod_perm;

    auto cons = ch.consumer(std::move(cons_perm));
    auto ep   = fpipe::mint_endpoint<Channel, fpipe::Direction::Consumer>(
        ctx, cons);

    int                not_a_log = 0;     // wrong type — not SessionEventLog&
    proto::RoleTagId   self{1};
    proto::RoleTagId   peer{2};

    // Second argument must be `SessionEventLog&`; passing `int`
    // fails reference binding.  fixy::bridge:: re-export must reject
    // identically — the using-decl preserves the exact parameter
    // shape.
    [[maybe_unused]] auto bad =
        fbridge::mint_recording_endpoint(
            std::move(ep), not_a_log, self, peer);

    return 0;
}
