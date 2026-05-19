// FIXY-U-084 HS14 neg-compile fixture (2 of 6).
//
// mint_hardening rejects HotFgCtx — the hot foreground context's
// row is empty (Row<>) and must never re-apply process-wide hardening.
// Re-applying a Policy from the hot path would re-issue every syscall
// in Hardening::apply() at hot-path cadence, defeating §VIII's "no
// syscalls on hot path" rule.  The §XXI factory is the type-level
// barrier; bypassing it via the bare apply() free function falls
// outside this fixture's scope.

#include <crucible/warden/Hardening.h>

int main() {
    crucible::warden::Policy p{};
    auto applied = crucible::warden::mint_hardening(
        crucible::effects::HotFgCtx{}, p);
    (void)applied;
    return 0;
}
