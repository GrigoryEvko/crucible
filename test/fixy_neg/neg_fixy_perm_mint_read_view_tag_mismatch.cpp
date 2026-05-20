// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #2 of 2 for fixy::perm::mint_read_view<Tag>
// (FIXY-U-014, mirrors safety_neg/neg_mint_read_view_explicit_tag_mismatch).
//
// Premise: `mint_read_view<TagB>(Permission<TagA> const&)` must
// REJECT at the fixy::perm:: import site because the explicit Tag
// must agree with the Tag carried by the Permission argument.
// Forging cross-region read access by routing through the alias is
// blocked at compile time, preserving the substrate's authority gate
// across the dual-export discipline.
//
// Distinct mismatch class from companion fixture
// neg_fixy_perm_mint_read_view_non_permission_arg.cpp:
//   * Companion:    PARAMETER-SHAPE gate (non-Permission argument).
//   * This fixture: TAG-IDENTITY gate (Permission<TagA> can't bind to
//                   Permission<TagB> const& parameter).
//
// Expected diagnostic: "Permission" appearing in the mismatch.

#include <crucible/fixy/Perm.h>

namespace neg_fixy_perm_read_view_tag_mismatch {
struct TagA {};
struct TagB {};
}  // namespace neg_fixy_perm_read_view_tag_mismatch

int main() {
    namespace tags  = neg_fixy_perm_read_view_tag_mismatch;
    namespace fperm = ::crucible::fixy::perm;
    namespace safe  = ::crucible::safety;

    auto perm_a = fperm::mint_permission_root<tags::TagA>();
    // Explicit ReadView<TagB> requires Permission<TagB>; perm_a's
    // type is Permission<TagA>, so the call-shape is ill-formed.
    auto view = fperm::mint_read_view<tags::TagB>(perm_a);
    (void)view;
    safe::permission_drop(std::move(perm_a));
    return 0;
}
