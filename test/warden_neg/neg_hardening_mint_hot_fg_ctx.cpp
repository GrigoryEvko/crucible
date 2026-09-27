// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_hardening refuses the hot foreground context.  Its row is empty,
// and a policy applied again from the hot path would issue every system
// call of Hardening::apply() at hot-path cadence.  The mint and
// Hardening::apply() take the same gate, so no route bypasses it.

#include <crucible/warden/Hardening.h>

int main() {
    crucible::warden::Policy p{};
    ::fixy::HotFgCtx const fg = ::foundation::effects::testing::foreground();
    auto applied = crucible::warden::mint_hardening(fg, p);
    (void)applied;
    return 0;
}
