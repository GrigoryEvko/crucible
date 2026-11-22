// Negative-compile test.  This file must fail to compile.
//
// mint_hardening refuses the cold init context.  That row is Init, Alloc
// and IO, with no Block.  The choice of the hot CPU reads sysfs and
// procfs, and a read can wait on the file system, so the mint asks for
// the startup load context, which also owns Block.

#include <crucible/warden/Hardening.h>

int main() {
    crucible::warden::Policy p{};
    ::fixy::ColdInitCtx const init{::foundation::effects::testing::init()};
    crucible::warden::TopologyRef const topology =
        ::fixy::erase_brand(::fixy::to(::fixy::topology::Topology::instance()));
    auto applied = crucible::warden::mint_hardening(init, topology, p);
    (void)applied;
    return 0;
}
