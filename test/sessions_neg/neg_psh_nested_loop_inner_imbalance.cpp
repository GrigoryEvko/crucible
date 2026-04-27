// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Fixture #8 — Nested Loop where the OUTER body balances but the
// INNER body does not.  Tests Decision D3's loop-context shadowing.
//
// THE BUG (looks REALLY innocent):
//   The user writes a two-level loop:
//
//     Outer: Loop< Recv<Transferable<int, X>, INNER> >
//     Inner: Loop< Send<Transferable<int, X>, Continue> >
//
//   Outer body recvs X, then enters inner loop.  Inner loop sends
//   X each iteration and Continue's back.  In the OUTER scope,
//   recv→INNER→Continue would balance: gain X, send X, end up at
//   {} which matches outer entry.  But the INNER Continue resolves
//   against INNER entry PS, not OUTER's, and inner entry PS is
//   {X} (acquired by outer recv just before entering inner loop).
//   Inner body's send drains X → inner Continue arrives at {} →
//   static_assert fires because inner entry PS was {X}.
//
// WHY THE TYPE SYSTEM CATCHES IT (Decision D3 with loop-context
// SHADOWING):
//   step_to_next_permissioned<Loop<Body>, ...> shadows LoopCtx
//   with LoopContext<Body, current_PS>.  When entering the inner
//   loop with PS = {X}, the inner loop context records its own
//   entry PS = {X}.  Inner Continue checks against INNER entry PS,
//   not OUTER.  This is what gives nested loops their own balance
//   constraint.
//
// WHY IT'S TRICKY:
//   The OUTER protocol IS balanced if you take inner+continue as a
//   single unit.  It's the INNER loop's per-iteration balance that
//   fails.  Reasoning about the right scope is the bug class users
//   miss; the type system gets it right by construction.

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

void wire_send(FakeChannel& ch, Transferable<int, WorkItem>&& t) noexcept {
    ch.last_int = t.value;
}

using InnerBody  = Send<Transferable<int, WorkItem>, Continue>;
using InnerLoop  = Loop<InnerBody>;
using OuterBody  = Recv<Transferable<int, WorkItem>, InnerLoop>;
using OuterLoop  = Loop<OuterBody>;
}

int main() {
    auto h = establish_permissioned<OuterLoop>(FakeChannel{});

    // Outer recv: PS becomes {WorkItem}.  Returned handle enters the
    // inner Loop, with InnerLoopCtx recording entry_perm_set = {X}.
    auto [val, h_inner] = std::move(h).recv(wire_recv);

    // Inner send: PS becomes {} after Transferable drains X.  The
    // returned handle steps to Continue, which checks against
    // INNER entry PS = {WorkItem} → fires balance assert.
    Transferable<int, WorkItem> payload{val.value, std::move(val.perm)};
    [[maybe_unused]] auto h_after = std::move(h_inner).send(std::move(payload),
                                                              wire_send);
    return 0;
}
