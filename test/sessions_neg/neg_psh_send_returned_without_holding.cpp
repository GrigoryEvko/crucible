// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Fixture #3 — Send<Returned<T, X>> from a handle whose PS does not
// contain X.
//
// THE BUG (looks innocent):
//   The user TRIES to "return" a Permission<X> they never actually
//   borrowed.  Returned is the round-trip half of the borrow-and-
//   return pattern; sending Returned<T, X> means "I'm giving you
//   back the X you lent me" — but the handle has no X to return.
//
// WHY THE TYPE SYSTEM CATCHES IT:
//   The same SendablePayload<T, PS> body static_assert that catches
//   Transferable also catches Returned: both markers' SendablePayload
//   disjuncts demand X ∈ PS.  PermSet evolution distinguishes them
//   semantically (Returned is the symmetric inverse of Transferable
//   at the protocol layer), but the SEND-SIDE precondition is the
//   same: the sender must hold the permission.
//
// WHY IT'S TRICKY:
//   Returned is semantically about giving BACK a permission, so a
//   user might wrongly think "if I never received it, I can fabricate
//   it as Returned to satisfy the protocol".  The type system blocks
//   this — you can't return what you never had.

#include <crucible/sessions/PermissionedSession.h>

using namespace crucible::safety::proto;
using ::crucible::safety::Permission;
using ::crucible::safety::mint_permission_root;

namespace {
struct HotPerm {};
struct FakeChannel { int last_int = 0; };

void wire_send(FakeChannel& ch, Returned<int, HotPerm>&& r) noexcept {
    ch.last_int = r.value;
}
}

int main() {
    // Establish without HotPerm.
    auto h = mint_permissioned_session<Send<Returned<int, HotPerm>, End>>(
        FakeChannel{});

    // Try to "return" a permission the handle never held.
    Returned<int, HotPerm> payload{99, mint_permission_root<HotPerm>()};
    [[maybe_unused]] auto h2 = std::move(h).send(std::move(payload),
                                                  wire_send);
    return 0;
}
