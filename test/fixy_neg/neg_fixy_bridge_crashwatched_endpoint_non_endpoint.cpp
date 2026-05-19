// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-HS14-04 negative fixture #1 (HS14 ≥2 floor):
// mint_crash_watched_endpoint first-parameter SFINAE / template-
// deduction route via the `fixy::bridge::` re-export.
//
// `mint_crash_watched_endpoint<PeerTag>(ep, flag)` wraps an
// Endpoint's bare session view in a CrashWatchedHandle so an
// upstream `OneShotFlag` gates the protocol against unreliable
// peers.  PeerTag is non-deducible (explicit); Substr / Dir / Ctx
// are deduced from the first parameter slot
// `Endpoint<Substr, Dir, Ctx>&&`.  Passing `int` fails template
// argument deduction because no Endpoint<Substr, Dir, Ctx>
// specialization matches `int`.
//
// This fixture routes through `fixy::bridge::mint_crash_watched_endpoint`
// (Bridge.h:154) — the §XXI canonical re-export.  Witnesses that
// the using-decl preserves the template-deduction gate on the
// endpoint-shaped first parameter.  A regression where the using-
// decl accidentally exposed a wider function-template forwarder
// accepting non-Endpoint args would slip past the substrate-side
// fixture while THIS fixture still rejected.
//
// Reject sequence: `fbridge::mint_crash_watched_endpoint<DeadPeer>(int, ...)`
// → template argument deduction attempts to deduce Substr / Dir /
// Ctx from `int&&` → no Endpoint<...> specialization matches →
// deduction fails → "no matching function for call to" / "could
// not deduce" / "no matching template".
//
// Distinct from fixture #2 (wrong_flag_type, the sibling): #1
// exercises the first-parameter template-deduction gate; #2
// exercises the second-parameter reference-binding gate against
// `OneShotFlag&` AFTER the first parameter binds successfully to a
// real Endpoint constructed via fpipe::mint_endpoint.  Different
// parameter slots, different failure mechanisms, different
// diagnostic shapes.
//
// Distinct from HS14-02 fixtures: HS14-02 exercises the substrate
// session-level mint (mint_crash_watched_session) — the class-
// template ctor (NoThrow gate) and the body static_assert
// (survivor_registry gate).  HS14-04 exercises the endpoint
// wrapper mint surface (mint_crash_watched_endpoint) which
// internally calls the session mint after consuming the Endpoint's
// bare session view.  Same wrapper family, distinct entry point,
// distinct signature surface.
//
// Expected diagnostic: "no matching function for call to
// 'mint_crash_watched_endpoint'" / "could not deduce" /
// "no matching template".

#include <crucible/fixy/Bridge.h>

namespace fbridge = ::crucible::fixy::bridge;
using ::crucible::safety::OneShotFlag;

// PeerTag must be supplied explicitly; choose any plausible tag.
struct DeadPeer {};

int main() {
    int         not_an_endpoint = 42;
    OneShotFlag flag;

    // First argument is `int` — fails Endpoint<Substr, Dir, Ctx>
    // template deduction.  fixy::bridge:: re-export must reject
    // identically — the using-decl preserves the template
    // signature.
    [[maybe_unused]] auto bad =
        fbridge::mint_crash_watched_endpoint<DeadPeer>(
            static_cast<int&&>(not_an_endpoint), flag);

    return 0;
}
