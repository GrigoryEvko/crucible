// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Fixture #6 — Recv Transferable then close at End without
// surrender.
//
// THE BUG (looks innocent):
//   The user writes a one-shot consumer: recv a Transferable<int, X>
//   from the channel, then close.  No surrender step in between.
//   Reaching End with X still held is a permission leak — the
//   receiver gained authority over the X region but never returned
//   it to the producer or the pool.
//
// WHY THE TYPE SYSTEM CATCHES IT:
//   close()'s static_assert(perm_set_equal_v<PS, EmptyPermSet>)
//   catches the leftover X.  Same diagnostic shape as fixture #1
//   but the leftover came from RECV evolution, not from initial
//   establish.
//
// WHY IT'S TRICKY:
//   In real code the recv often happens inside a function that
//   returns the value to the caller — the permission "should" flow
//   to whoever uses the value, but the protocol-level type system
//   sees only what the SESSION did, not what the application code
//   does after.  Many users assume that handing val to caller is
//   equivalent to surrendering the permission; the type system
//   forces the surrender to be EXPLICIT in the protocol.

#include <crucible/sessions/PermissionedSession.h>

using namespace crucible::safety::proto;
using ::crucible::safety::Permission;
using ::crucible::safety::permission_root_mint;

namespace {
struct WorkItem {};
struct FakeChannel { int last_int = 0; };

Transferable<int, WorkItem> wire_recv(FakeChannel& ch) noexcept {
    return Transferable<int, WorkItem>{ch.last_int,
                                        permission_root_mint<WorkItem>()};
}
}

int main() {
    auto h = establish_permissioned<
        Recv<Transferable<int, WorkItem>, End>>(FakeChannel{});

    auto [val, h2] = std::move(h).recv(wire_recv);
    (void)val;
    // h2 is at End with PS = {WorkItem} (gained from recv).
    // close() static_assert fires.
    [[maybe_unused]] auto out = std::move(h2).close();
    return 0;
}
