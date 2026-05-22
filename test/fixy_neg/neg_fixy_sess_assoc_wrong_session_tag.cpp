// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: `fsassoc::assert_associated<Γ, G, MySession>()` where Γ
// is structurally a valid reflexive Δ for G but every Entry is tagged
// with a DIFFERENT session (OtherSession).  Γ's domain RESTRICTED to
// MySession is empty (no entries match the SessionTag filter), while
// G has non-empty roles → HYK24 condition (1) fails.
//
// FIXY-V-059 HS14 floor — fixture 3 of 3.  Pairs with:
//   1. neg_fixy_sess_assoc_domain_mismatch_missing_role.cpp
//      (right tag, fewer roles)
//   2. neg_fixy_sess_assoc_role_list_missing_coverage.cpp
//      (right tag, wrong role identities)
//
// This fixture's role: catches the bug class where a multi-session
// pipeline has a typo'd SessionTag at the assertion site — even
// though Γ DOES contain valid entries (just for the wrong session
// tag), the assertion against MySession fails because Γ's
// domain-restricted-to-MySession is empty.  Without this gate the
// assertion would silently misfire on the wrong session's context.
//
// Note: assert_associated<Γ, OtherSession>() WOULD succeed (Γ IS a
// valid reflexive Δ for the OtherSession projection of G).  The bug
// is at the CALL SITE — passing the wrong tag at the boundary.

#include <crucible/fixy/SessAssoc.h>
#include <crucible/sessions/Session.h>
#include <crucible/sessions/SessionContext.h>
#include <crucible/sessions/SessionGlobal.h>

namespace fsassoc = ::crucible::fixy::sess::assoc;
namespace proto   = ::crucible::safety::proto;

namespace {
struct MySession    {};
struct OtherSession {};
struct Coord    {};
struct Follower {};
struct Prepare  {};
struct Vote     {};

using G = proto::Transmission<Coord, Follower, Prepare,
          proto::Transmission<Follower, Coord, Vote, proto::End_G>>;

// Γ has Coord + Follower entries (matches G's roles) but ALL tagged
// with OtherSession.  An assertion against MySession will find an
// EMPTY domain for that tag in Γ.
using WrongTagGamma = proto::Context<
    proto::Entry<OtherSession, Coord,    proto::project_t<G, Coord>>,
    proto::Entry<OtherSession, Follower, proto::project_t<G, Follower>>>;
}  // namespace

int main() {
    // Should FAIL: Γ's domain restricted to MySession is empty,
    // while G has non-empty roles — condition (1) of HYK24
    // association fails.  This pins the wrong-tag-at-call-site
    // bug class.
    fsassoc::assert_associated<WrongTagGamma, G, MySession>();
    return 0;
}
