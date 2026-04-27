// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-A23 — split_grid<Whole, 0, 1>(parent) at the FACTORY call
// site must be rejected.  Even if the user managed to bypass the
// auto_split_grid type-level static_assert, the factory call must
// independently re-check, because callers naming wrong M/N at the
// call boundary deserve their own pinpointed diagnostic.
//
// Expected diagnostic substring:
//   "split_grid<Whole, M, N>: M and N must both be greater than zero"

#include <crucible/permissions/Permission.h>
#include <crucible/safety/PermissionGridGenerator.h>

namespace {
struct GridTag {};
}  // namespace

void exercise_zero_M_call() {
    auto p = crucible::safety::permission_root_mint<GridTag>();
    // (M=0, N=1) — fails the factory-side static_assert.
    [[maybe_unused]] auto bad = crucible::safety::split_grid<GridTag, 0, 1>(
        std::move(p));
}

int main() { return 0; }
