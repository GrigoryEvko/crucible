// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Fixture #4 — Loop body that DRAINS a permission per iteration
// without surrender.
//
// THE BUG (looks innocent):
//   The user writes a perpetual-worker loop that establishes with
//   PermSet<X> and sends Transferable<int, X> on each iteration.
//   Each iteration successfully sends X (loses it from PS), then
//   loops back via Continue.  But Continue must restore the
//   loop-entry PS — and the entry was {X}, while Continue arrives
//   at {} (X was drained).  The loop is structurally unbalanced:
//   iteration 2 would try to send X again with no X to send.
//
// WHY THE TYPE SYSTEM CATCHES IT (Decision D3 / Risk R1):
//   step_to_next_permissioned<Continue, ...>'s static_assert checks
//   perm_set_equal_v<PS_at_continue, LoopEntryPS>.  Here that's
//   perm_set_equal_v<EmptyPermSet, PermSet<X>> → FAILS with the
//   [PermissionImbalance] diagnostic about loop-iteration balance.
//
// WHY IT'S TRICKY:
//   The loop body LOOKS OK in isolation — the send is well-typed,
//   the protocol shape is well-formed (Send<...>, Continue is
//   inside a Loop).  The bug is INVARIANT-LEVEL: the body's PS
//   evolution doesn't loop back to itself.  Hard to spot in code
//   review without tracing PS evolution by hand; the type system
//   does the trace automatically.

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

using BodyProto = Send<Transferable<int, WorkItem>, Continue>;
using LoopProto = Loop<BodyProto>;
}

int main() {
    auto perm = mint_permission_root<WorkItem>();
    auto h = mint_permissioned_session<LoopProto>(FakeChannel{},
                                                std::move(perm));

    Transferable<int, WorkItem> payload{1, mint_permission_root<WorkItem>()};
    // The send compiles (PS contains WorkItem at body entry); but the
    // returned handle's step-to-Continue resolution fires the loop-
    // balance assert because PS at Continue is Empty, not {WorkItem}.
    [[maybe_unused]] auto h2 = std::move(h).send(std::move(payload),
                                                  wire_send);
    return 0;
}
