// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A1-020 — second HS14 mismatch class for mint_grid_permissions.
//
// §XXI Universal Mint Pattern: the factory takes Permission<Whole>&&
// by rvalue.  Passing a non-Permission argument (or a wrong-tag
// Permission) is a parameter-shape mismatch — overload resolution
// fails BEFORE the requires-clause is even evaluated.
//
// This is a distinct mismatch class from neg_mint_grid_permissions_
// call_zero.cpp (which tests the can_split_grid_v requires-clause
// fold).  Per HS14, every §XXI mint factory ships at least one
// negative-compile fixture per distinct mismatch class.
//
// Expected diagnostic substring:
//   "no match" / "no matching function" / "could not convert" /
//   "candidate expects" / "cannot bind"

#include <crucible/permissions/Permission.h>
#include <crucible/safety/PermissionGridGenerator.h>

namespace {
struct GridTag {};

void exercise_non_permission_arg() {
    // The factory parameter is Permission<GridTag>&&.  An int rvalue
    // cannot bind to that parameter — the call has no viable overload.
    [[maybe_unused]] auto bad = crucible::safety::mint_grid_permissions<GridTag, 2, 2>(42);
}
}  // namespace

int main() { exercise_non_permission_arg(); return 0; }
