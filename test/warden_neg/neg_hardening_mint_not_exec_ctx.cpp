// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_hardening refuses a type that is not an execution context.  The
// gate is IsExecCtx<Ctx> and a row that holds Init.  The two fixtures for
// the background and foreground contexts reach the row half.  This one
// reaches the IsExecCtx half with a plain struct.

#include <crucible/warden/Hardening.h>

struct NotAnExecCtx {};

int main() {
    crucible::warden::Policy p{};
    auto applied = crucible::warden::mint_hardening(NotAnExecCtx{}, p);
    (void)applied;
    return 0;
}
