// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: `fsassoc::assert_associated<Γ, G, SessionTag>()` where Γ
// is missing a role that G has.  HYK24 condition (1) (dom(Γ) = roles
// (G)) fails.  The substrate's classified static_assert fires with the
// `Association_Domain_Mismatch` diagnostic; this fixture pins the
// rejection AT the fixy::sess::assoc:: surface boundary so a future
// regression that strips the fixy re-export wrapper reds at HS14.
//
// FIXY-V-059 HS14 floor — fixture 1 of 3.  Pairs with:
//   2. neg_fixy_sess_assoc_role_list_missing_coverage.cpp
//      (Γ has wrong-named role, same cardinality)
//   3. neg_fixy_sess_assoc_wrong_session_tag.cpp
//      (Γ tagged with different session, assert with the wrong one)
//
// All three trip condition (1) but via DIFFERENT shapes:
//   * shape A (THIS file): Γ ⊂ G  (Γ is a proper subset of G's roles)
//   * shape B (#2):        Γ ↔ G  (Γ has the same cardinality but
//                          different role identity — a typo-grade
//                          mismatch)
//   * shape C (#3):        Γ ↦ wrong-tag  (Γ tagged with a different
//                          session, domain-for-MyTag is empty)
//
// Rationale for routing through fixy::sess::assoc: the substrate's
// own neg_session_assoc_missing_role.cpp already pins the substrate-
// side rejection.  This fixture witnesses that the SAME rejection
// continues to fire when the call site spells the assertion through
// the FIXY UMBRELLA — guarding against a future refactor that
// silently breaks the using-decl chain.

#include <crucible/fixy/SessAssoc.h>
#include <crucible/sessions/Session.h>
#include <crucible/sessions/SessionContext.h>
#include <crucible/sessions/SessionGlobal.h>

namespace fsassoc = ::crucible::fixy::sess::assoc;
namespace proto   = ::crucible::safety::proto;

namespace {
struct My2PC    {};
struct Coord    {};
struct Follower {};
struct Prepare  {};
struct Vote     {};

using G = proto::Transmission<Coord, Follower, Prepare,
          proto::Transmission<Follower, Coord, Vote, proto::End_G>>;

// G has roles {Coord, Follower}.  Γ has ONLY Coord — Follower
// missing.  Domain mismatch: condition (1) fails.
using IncompleteGamma = proto::Context<
    proto::Entry<My2PC, Coord, proto::project_t<G, Coord>>>;
}  // namespace

int main() {
    // Should FAIL: assert_associated fires the
    // condition-(1) static_assert with the
    // `Association_Domain_Mismatch` diagnostic.
    fsassoc::assert_associated<IncompleteGamma, G, My2PC>();
    return 0;
}
