// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Fixture #2 — Send<Transferable<T, X>> from a handle whose PS does
// not contain X.
//
// THE BUG (looks innocent):
//   The user calls send() with what LOOKS like a valid Transferable
//   payload — they construct it inline, and the wire-format Transport
//   accepts it.  But the type-level PermSet on the handle does not
//   actually contain the X tag, so the send is a fabrication: there's
//   no real Permission<X> being moved through the protocol — the
//   payload was constructed with mint_permission_root at the call
//   site, which is the canonical (grep-discoverable) mint pattern,
//   but minting a fresh root is NOT the same as transferring an
//   existing held permission.
//
// WHY THE TYPE SYSTEM CATCHES IT:
//   PermissionedSessionHandle<Send<T, R>, PS, R, L>::send() carries
//   a body static_assert(SendablePayload<T, PS>) with the
//   [PermissionImbalance] diagnostic prefix.  SendablePayload
//   demands that for Transferable<T, X>, X ∈ PS.
//
// WHY IT'S TRICKY:
//   The user CAN call mint_permission_root<X>() inline because mint
//   is review-discoverable, not type-blocked.  The protocol-level
//   check is what enforces "X is in flight from sender to receiver",
//   and that's the static_assert here.

#include <crucible/sessions/PermissionedSession.h>

using namespace crucible::safety::proto;
using ::crucible::safety::Permission;
using ::crucible::safety::mint_permission_root;

namespace {
struct WorkItem {};
struct FakeChannel { int last_int = 0; };

void wire_send(FakeChannel& ch, Transferable<int, WorkItem>&& t) noexcept {
    ch.last_int = t.value;
}
}

int main() {
    // Establish without holding WorkItem — PS == EmptyPermSet.
    auto h = mint_permissioned_session<Send<Transferable<int, WorkItem>, End>>(
        FakeChannel{});

    // User constructs payload inline using mint_permission_root —
    // looks like a valid Transferable, but the handle has no PS to
    // back it.  The send's body static_assert fires.
    Transferable<int, WorkItem> payload{42, mint_permission_root<WorkItem>()};
    [[maybe_unused]] auto h2 = std::move(h).send(std::move(payload),
                                                  wire_send);
    return 0;
}
