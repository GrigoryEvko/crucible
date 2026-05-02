// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Fixture #5 — Loop body that GAINS a permission per iteration
// without surrender (the SYMMETRIC inverse of fixture #4).
//
// THE BUG (looks innocent):
//   The user writes a recv-only loop that recvs Transferable<int, X>
//   each iteration and loops back.  Each recv ADDS X to PS.  Loop
//   entry PS = {}; after one recv, PS = {X}; Continue would try
//   to restore entry PS = {} — but the body left {X}.
//
// WHY THE TYPE SYSTEM CATCHES IT (Decision D3 / Risk R1):
//   Same as fixture #4 — the Loop balance assert fires at Continue.
//   The semantic is different (here the bug is "permission leak in
//   the receiver" rather than "drain in the sender"), but the type
//   system catches both via the same invariant.
//
// WHY IT'S TRICKY:
//   This bug is INSIDIOUS in production: the receiver looks like a
//   normal worker that consumes work-items, but it's accumulating
//   permissions across iterations.  In a long-running server,
//   iteration N would have {X1, X2, ..., XN} in PS even though only
//   ONE work-item is in flight at a time.  The type system makes
//   this impossible to compile.

#include <crucible/sessions/PermissionedSession.h>

using namespace crucible::safety::proto;
using ::crucible::safety::Permission;
using ::crucible::safety::mint_permission_root;

namespace {
struct WorkItem {};
struct FakeChannel { int last_int = 0; };

Transferable<int, WorkItem> wire_recv(FakeChannel& ch) noexcept {
    return Transferable<int, WorkItem>{ch.last_int,
                                        mint_permission_root<WorkItem>()};
}

using BodyProto = Recv<Transferable<int, WorkItem>, Continue>;
using LoopProto = Loop<BodyProto>;
}

int main() {
    // Establish with empty PS (entry is empty).
    auto h = mint_permissioned_session<LoopProto>(FakeChannel{});

    // recv adds WorkItem to PS; the resulting next-handle on
    // Continue fires the balance assert (PS at Continue = {WorkItem},
    // entry PS = {}).
    [[maybe_unused]] auto [val, h2] = std::move(h).recv(wire_recv);
    return 0;
}
