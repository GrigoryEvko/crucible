// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: `fsassoc::assert_associated<Γ, G, SessionTag>()` where Γ
// has the same cardinality of roles as G but the role IDENTITIES
// don't match — typo-grade coverage mismatch.  G's roles are {Coord,
// Follower}; Γ's roles are {Coord, Stranger}.  HYK24 condition (1)
// fails because Stranger ∉ roles(G).
//
// FIXY-V-059 HS14 floor — fixture 2 of 3.  Pairs with:
//   1. neg_fixy_sess_assoc_domain_mismatch_missing_role.cpp
//      (Γ ⊂ G — strict-subset shape)
//   3. neg_fixy_sess_assoc_wrong_session_tag.cpp
//      (Γ ↦ wrong-tag — empty-domain-for-tag shape)
//
// This fixture's role: distinguish "same cardinality, wrong identity"
// from "smaller cardinality".  A buggy implementation that checked
// only |dom(Γ)| = |roles(G)| would PASS this case while still
// failing fixture 1 — and would silently corrupt the association
// invariant for any (G, Γ) pair where role names got renamed
// without the corresponding context updates.  Pinning both shapes
// catches the cardinality-only regression class.

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
struct Stranger {};   // NOT in G's role set — the bug
struct Prepare  {};
struct Vote     {};

using G = proto::Transmission<Coord, Follower, Prepare,
          proto::Transmission<Follower, Coord, Vote, proto::End_G>>;

// |Γ's domain for My2PC| = 2 (matches G's |roles| = 2) but the
// IDENTITIES differ: Stranger ∉ roles(G).  Cardinality is right;
// role-list coverage isn't.  Condition (1) fails.
using TypoGamma = proto::Context<
    proto::Entry<My2PC, Coord,    proto::project_t<G, Coord>>,
    proto::Entry<My2PC, Stranger, proto::End>>;   // Stranger ∉ G
}  // namespace

int main() {
    // Should FAIL: same-cardinality-but-wrong-identity is REJECTED
    // by HYK24's set-equality check at condition (1).
    fsassoc::assert_associated<TypoGamma, G, My2PC>();
    return 0;
}
