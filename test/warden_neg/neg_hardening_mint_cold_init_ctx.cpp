// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_hardening refuses the cold init context.  That row is Init, Alloc
// and IO, with no Block.  The choice of the hot CPU reads sysfs and
// procfs, and a read can wait on the file system, so the mint asks for
// the startup load context, which also owns Block.

#include <crucible/warden/Hardening.h>

int main() {
    crucible::warden::Policy p{};
    ::fixy::ColdInitCtx const init{::foundation::effects::testing::init()};
    auto applied = crucible::warden::mint_hardening(init, p);
    (void)applied;
    return 0;
}
