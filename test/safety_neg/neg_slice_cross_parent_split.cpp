// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-A23 — splitting a Permission<ParentA> into Slice<ParentB, ...>
// children must be rejected.  The auto-generated splits_into_pack
// specialization fires only for Slice<Parent, Is>... where Parent
// matches the Permission's tag.  Cross-parent splits must hit the
// permission_split_n's own framework static_assert, naming the
// missing splits_into_pack specialization.
//
// Expected diagnostic substring (FRAMEWORK-CONTROLLED — taken from
// permission_split_n's own static_assert message):
//   "permission_split_n<Children...>"

#include <crucible/permissions/Permission.h>
#include <crucible/safety/PermissionTreeGenerator.h>

namespace {
struct ParentA {};
struct ParentB {};   // distinct parent — children of ParentB cannot
                     // be derived from a Permission<ParentA>
}  // namespace

void exercise_cross_parent() {
    auto p = crucible::safety::permission_root_mint<ParentA>();
    // Try to split a Permission<ParentA> into Slice<ParentB, 0>...
    // — splits_into_pack<ParentA, Slice<ParentB, 0>, ...> is FALSE
    // (no specialization fires; the universal one requires Parent
    // to match), so the framework static_assert fires.
    using SliceB0 = crucible::safety::Slice<ParentB, 0>;
    using SliceB1 = crucible::safety::Slice<ParentB, 1>;
    [[maybe_unused]] auto bad =
        crucible::safety::permission_split_n<SliceB0, SliceB1>(std::move(p));
}

int main() { return 0; }
