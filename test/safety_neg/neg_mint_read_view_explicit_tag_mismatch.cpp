// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #2 of 2 for safety::mint_read_view<Tag> (fixy-A1-019).
//
// Premise: mint_read_view<Tag>(Permission<Tag> const&) requires the
// explicit Tag to match the Tag carried by the argument's
// Permission.  Passing Permission<TagA> while explicitly requesting
// ReadView<TagB> must be rejected at compile time — otherwise a caller
// could mint a ReadView<TagB> from authority they never possessed,
// forging cross-region read access.
//
// Distinct mismatch class from companion fixture
// neg_mint_read_view_non_permission_arg.cpp:
//   * Companion:    PARAMETER-SHAPE gate (non-Permission argument).
//   * This fixture: TAG-IDENTITY gate (Permission<TagA> can't bind
//                   to Permission<TagB> const& parameter).

#include <crucible/permissions/Permission.h>
#include <crucible/permissions/ReadView.h>

namespace {
struct TagA {};
struct TagB {};
}

int main() {
    auto perm_a = crucible::safety::mint_permission_root<TagA>();
    // Explicit ReadView<TagB> conflicts with the argument's
    // Permission<TagA> — Tag identity mismatch.
    auto view = crucible::safety::mint_read_view<TagB>(perm_a);
    (void)view;
    return 0;
}
