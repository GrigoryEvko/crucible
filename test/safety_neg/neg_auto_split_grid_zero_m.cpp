// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-A23 — auto_split_grid<Whole, 0, N> must be rejected.  A grid
// with zero producers cannot be a producer-consumer mesh; the type
// silently degrading to "broadcast-only" would mislead the dispatcher.
// The static_assert inside auto_split_grid's body catches this.
//
// Expected diagnostic substring:
//   "auto_split_grid<Whole, M, N>: M (producer count) must be > 0"

#include <crucible/safety/PermissionGridGenerator.h>

namespace {
struct GridTag {};
}  // namespace

// Force instantiation so the M > 0 static_assert fires.
using BadGrid = crucible::safety::auto_split_grid<GridTag, 0, 4>;
[[maybe_unused]] BadGrid::producer_perms dummy;

int main() { return 0; }
