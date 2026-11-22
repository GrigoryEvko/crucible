// Negative-compile test.  This file must fail to compile.
//
// mint_hardening refuses a type that is not an execution context.  The
// gate is IsExecCtx<Ctx> and a row that holds Init.  The two fixtures for
// the background and foreground contexts reach the row half.  This one
// reaches the IsExecCtx half with a plain struct.

#include <crucible/warden/Hardening.h>

struct NotAnExecCtx {};

int main() {
    crucible::warden::Policy p{};
    crucible::warden::TopologyRef const topology =
        ::fixy::erase_brand(::fixy::to(::fixy::topology::Topology::instance()));
    auto applied = crucible::warden::mint_hardening(NotAnExecCtx{}, topology, p);
    (void)applied;
    return 0;
}
