// Negative-compile test.  This file must fail to compile.
//
// mint_hardening refuses the hot foreground context.  Its row is empty,
// and a policy applied again from the hot path would issue every system
// call of Hardening::apply() at hot-path cadence.  The mint and
// Hardening::apply() take the same gate, so no route bypasses it.

#include <crucible/warden/Hardening.h>

int main() {
    crucible::warden::Policy p{};
    ::fixy::HotFgCtx const fg = ::foundation::effects::testing::foreground();
    crucible::warden::TopologyRef const topology =
        ::fixy::erase_brand(::fixy::to(::fixy::topology::Topology::instance()));
    auto applied = crucible::warden::mint_hardening(fg, topology, p);
    (void)applied;
    return 0;
}
