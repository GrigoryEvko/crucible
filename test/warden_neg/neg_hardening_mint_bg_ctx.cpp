// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_hardening refuses a context whose effect row lacks Init.  The row
// of the background drain context is Bg and Alloc.  The hardening system
// calls (sched_setaffinity, mlock2, madvise, prctl) change process-wide
// state, which is start-up work, so a drain context must not reach them.

#include <crucible/warden/Hardening.h>

int main() {
    crucible::warden::Policy p{};
    ::fixy::BgDrainCtx const bg{::foundation::effects::testing::bg()};
    crucible::warden::TopologyRef const topology =
        ::fixy::erase_brand(::fixy::to(::fixy::topology::Topology::instance()));
    auto applied = crucible::warden::mint_hardening(bg, topology, p);
    (void)applied;
    return 0;
}
