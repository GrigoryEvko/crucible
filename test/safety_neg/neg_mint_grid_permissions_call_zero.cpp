// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A1-020 / FOUND-A23 — mint_grid_permissions<Whole, 0, 1>(parent)
// at the FACTORY call site must be rejected.  The §XXI Universal Mint
// Pattern moves the M>0 / N>0 check from a body-level static_assert
// into the requires-clause (can_split_grid_v<Whole, M, N>), so callers
// naming wrong M/N hit the constraint at OVERLOAD RESOLUTION rather
// than after the body has instantiated.
//
// Expected diagnostic substring:
//   "can_split_grid_v" / "constraints not satisfied"

#include <crucible/permissions/Permission.h>
#include <crucible/safety/PermissionGridGenerator.h>

namespace {
struct GridTag {};
}  // namespace

void exercise_zero_M_call() {
    auto p = crucible::safety::mint_permission_root<GridTag>();
    // (M=0, N=1) — fails the requires-clause can_split_grid_v.
    [[maybe_unused]] auto bad = crucible::safety::mint_grid_permissions<GridTag, 0, 1>(
        std::move(p));
}

int main() { return 0; }
