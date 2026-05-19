// FIXY-U-084 HS14 neg-compile fixture (1 of 6).
//
// mint_hardening rejects a context whose effect row lacks Init.
// BgDrainCtx::row = Row<Bg, Alloc> — no Init grant.  Hardening syscalls
// (sched_setaffinity, mlock2, madvise, prctl) are process-wide state
// mutations belonging to the startup-only Init row; the Bg drain
// context must NOT engage this surface.

#include <crucible/warden/Hardening.h>

int main() {
    crucible::warden::Policy p{};
    auto applied = crucible::warden::mint_hardening(
        crucible::effects::BgDrainCtx{}, p);
    (void)applied;
    return 0;
}
