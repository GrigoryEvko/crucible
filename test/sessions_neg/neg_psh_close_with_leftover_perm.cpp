// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Fixture #1 — Close at End with leftover Permission.
//
// THE BUG (looks innocent):
//   The user mints a Permission<WorkItem> at startup, threads it
//   into a permissioned session that establishes on `End`, then
//   immediately closes.  No protocol step ever surrenders the
//   permission — the close at End sees PS = {WorkItem} and the
//   permission is leaked into the void.
//
// WHY THE TYPE SYSTEM CATCHES IT:
//   PermissionedSessionHandle<End, PS, R, L>::close() carries a
//   `static_assert(perm_set_equal_v<PS, EmptyPermSet>)` with the
//   [PermissionImbalance] diagnostic prefix.  The user MUST surrender
//   every Transferable-acquired permission before reaching End, or
//   the type system rejects the close.
//
// WHY IT'S TRICKY:
//   In single-step protocols this looks like an obvious "missing
//   send" bug, but the SAME pattern in production hides behind a
//   long Recv chain — the user might recv 5 permissions across 5
//   protocol steps and forget to surrender ONE before End.  The
//   type system catches the SUM at close, not at the missing step.

#include <crucible/sessions/PermissionedSession.h>

using namespace crucible::safety::proto;
using ::crucible::safety::Permission;
using ::crucible::safety::mint_permission_root;

namespace {
struct WorkItem {};
struct FakeChannel { int last_int = 0; };
}

int main() {
    auto perm = mint_permission_root<WorkItem>();
    auto h = mint_permissioned_session<End>(FakeChannel{},
                                          std::move(perm));
    // PS at this point: PermSet<WorkItem>.
    // close() requires PS == EmptyPermSet → fires.
    [[maybe_unused]] auto out = std::move(h).close();
    return 0;
}
