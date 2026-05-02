// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Fixture #7 — Select with a branch that doesn't surrender the
// permission, exercising Decision D4 STRUCTURAL convergence.
//
// THE BUG (looks innocent):
//   The user writes Select<End, Send<Transferable<int, X>, End>>
//   and establishes with PermSet<X>.  Branch 0 is "just close" —
//   reaches End with PS still containing X.  Branch 1 surrenders
//   X via Send and reaches End with empty PS.  The user picks
//   branch 0 to "skip the work" but forgets that branch 0 must
//   ALSO surrender X to converge with branch 1.
//
// WHY THE TYPE SYSTEM CATCHES IT (Decision D4 STRUCTURAL):
//   Decision D4 — branch convergence is enforced STRUCTURALLY at
//   each branch's terminal head, NOT via a separate "all branches
//   converge" metafunction at the Select.  Branch 0 reaches End
//   with PS = {X}; close()'s static_assert fires.  Branch 1 reaches
//   End with PS = {} (the send drained X); close() OK.
//
//   So the user can WRITE the Select, can call select_local<0>(),
//   but cannot CLOSE the resulting branch-0 handle.  This is the
//   compositional design — convergence is checked at the convergence
//   point, not at the divergence point.
//
// WHY IT'S TRICKY:
//   In code review this looks like a valid Select — both branches
//   reach End, the protocol shape is well-formed, the static
//   convergence rule (all branches reach a terminal of the same
//   shape) is satisfied.  The PERMISSION-FLOW divergence is invisible
//   in the protocol type alone; you have to trace what each branch
//   does to PS.  The type system forces the trace.

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
    // Two branches: one closes immediately (leaks X), one drains
    // X via send.  Establish with X.
    auto h = mint_permissioned_session<
        Select<End,
               Send<Transferable<int, WorkItem>, End>>>(
        FakeChannel{}, std::move(perm));

    // Pick branch 0 — close at End with PS = {WorkItem} fires.
    auto h0 = std::move(h).template select_local<0>();
    [[maybe_unused]] auto out = std::move(h0).close();
    return 0;
}
