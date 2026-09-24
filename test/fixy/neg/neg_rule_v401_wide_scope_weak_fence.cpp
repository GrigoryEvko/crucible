// V401: a memory scope at or above Cluster with a barrier below AcqRel.
//
// The publication reaches further than the fence orders.  A cluster or
// device-wide scope says every thread that far away may observe the
// value; a release-only or acquire-only fence orders one direction of
// that observation and leaves the other to chance.
//
// This rule reads two axes, and it went live with the second of them:
// BarrierStrength shipped first, and this fixture arrived with
// MemoryScope.  The scope side is the two-trunk lattice, and the cell
// beside this one in test/fixy/test_collision.cpp shows a host-trunk
// scope (Inner) is NOT at or above Cluster — incomparable, not smaller —
// so the rule cannot fire on it.

#include <fixy/Fn.h>

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::scope::cluster, ::fixy::atom::barrier::release_store> refused{};
    return 0;
}
