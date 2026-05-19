// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-HS14-04 negative fixture #2 (HS14 ≥2 floor):
// mint_crash_watched_endpoint second-parameter reference-binding
// route via the `fixy::bridge::` re-export — distinct mismatch
// class from fixture #1 (non_endpoint / first-param template-
// deduction).
//
// `mint_crash_watched_endpoint<PeerTag>(ep, flag)` takes
// `OneShotFlag&` as its second parameter.  After the first
// parameter binds successfully (the inbound argument IS a real
// Endpoint constructed via fpipe::mint_endpoint<Channel,
// Direction::Consumer>(ctx, cons)) and the registered PeerTag
// passes the body's `require_crash_survivors_declared_<PeerTag>()`
// static_assert, the second parameter's reference type fails to
// bind against a non-OneShotFlag argument.
//
// Why this is a DISTINCT mismatch class from fixture #1:
//   - Fixture #1 (non_endpoint): first-parameter template-
//     deduction.  Substr / Dir / Ctx cannot be deduced from `int`.
//     Diagnostic: "no matching function" / "could not deduce" /
//     "no matching template" against the whole template.
//   - Fixture #2 (wrong_flag_type, this file): first-parameter
//     SUCCESSFULLY DEDUCES (the inbound IS a real Endpoint), the
//     PeerTag's survivor_registry is properly specialized so the
//     body static_assert passes, and the failure is post-deduction
//     reference-binding on the SECOND parameter: `OneShotFlag&`
//     cannot bind to `int`.  Diagnostic: "cannot bind non-const
//     lvalue reference of type 'OneShotFlag&'" / "could not
//     convert" / "no matching function".
//
// Why DeadPeer carries an explicit `survivor_registry`
// specialization: without it, the body static_assert
// `require_crash_survivors_declared_<PeerTag>()` would ALSO fire,
// muddying the diagnostic and conflating with HS14-02 fixture #2
// (substrate session-level unregistered_peer).  Specializing
// survivor_registry<DeadPeer> here isolates the failure to ONLY
// the second-param reference-binding gate, which is the discipline
// we want to pin.
//
// Distinct from HS14-02 fixtures: HS14-02 exercises substrate
// session-level mint (mint_crash_watched_session) at the class-
// template ctor + body static_assert.  HS14-04 exercises the
// endpoint mint surface (mint_crash_watched_endpoint).
//
// The `fixy::bridge::` re-export must reject identically — the
// using-decl preserves the exact parameter shape, not just the
// requires-clause.
//
// Expected diagnostic: "no matching function for call to
// 'mint_crash_watched_endpoint'" OR "cannot bind non-const lvalue
// reference of type 'OneShotFlag&'" OR "could not convert".

#include <crucible/concurrent/PermissionedSpscChannel.h>
#include <crucible/effects/ExecCtx.h>
#include <crucible/fixy/Bridge.h>
#include <crucible/fixy/Pipe.h>
#include <crucible/permissions/Permission.h>
#include <crucible/permissions/PermissionInherit.h>

#include <utility>

namespace eff     = ::crucible::effects;
namespace conc    = ::crucible::concurrent;
namespace fbridge = ::crucible::fixy::bridge;
namespace fpipe   = ::crucible::fixy::pipe;
namespace saf     = ::crucible::safety;

struct EndpointTag {};
struct DeadPeer    {};
struct Survivor    {};

using Channel = conc::PermissionedSpscChannel<int, 64, EndpointTag>;

// Register the survivor inheritance so the body static_assert
// `require_crash_survivors_declared_<DeadPeer>()` passes.  Without
// this the diagnostic would conflate with HS14-02 fixture #2.
namespace crucible::permissions {

template <>
struct survivor_registry<DeadPeer> {
    using type = inheritance_list<Survivor>;
};

}  // namespace crucible::permissions

int main() {
    eff::HotFgCtx ctx;

    Channel ch;

    // Build a valid consumer Endpoint.  First-param gate
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

    int not_a_flag = 0;     // wrong type — not OneShotFlag&

    // Second argument must be `OneShotFlag&`; passing `int` fails
    // reference binding.  fixy::bridge:: re-export must reject
    // identically — the using-decl preserves the exact parameter
    // shape.
    [[maybe_unused]] auto bad =
        fbridge::mint_crash_watched_endpoint<DeadPeer>(
            std::move(ep), not_a_flag);

    return 0;
}
