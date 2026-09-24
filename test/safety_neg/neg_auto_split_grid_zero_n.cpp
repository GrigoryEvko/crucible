// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-A23 — auto_split_grid<Whole, M, 0> must be rejected.
// Symmetric to neg_auto_split_grid_zero_m: a grid with zero
// consumers is structurally meaningless.
//
// Expected diagnostic substring:
//   "auto_split_grid<Whole, M, N>: N (consumer count) must be > 0"

#include <crucible/safety/PermissionGridGenerator.h>

namespace {
struct GridTag {};
}  // namespace

using BadGrid = crucible::safety::auto_split_grid<GridTag, 4, 0>;
[[maybe_unused]] BadGrid::consumer_perms dummy;

int main() { return 0; }
