// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fix-07: mint_permission_split<L, R> requires the two child region
// tags L and R to be DISTINCT.  A manifest author who writes
// `splits_into<Whole, A, A>` (the same tag twice) would otherwise mint
// two `Permission<A>` from one parent — two linear tokens for the SAME
// region.  That aliases the very disjointness the CSL frame rule exists
// to prove: both "halves" name region A, so a downstream
// permission_fork could hand two threads a Permission<A> each and race.
//
// The new pairwise-distinctness static_assert inside mint_permission_split
// rejects the call.  The diagnostic carries the substring "fix-07" and
// "DISTINCT".
//
// Note the splits_into<Whole, A, A> + authoring-witness specializations
// are themselves well-formed (C++ has no orphan rule); the guard fires
// at the MINT boundary, exactly where authority would be forged.

#include <crucible/permissions/Permission.h>

#include <utility>

namespace neg_permission_split_same_tag {

struct Whole {};
struct A {};

}  // namespace neg_permission_split_same_tag

namespace crucible::safety {

template <>
struct splits_into<neg_permission_split_same_tag::Whole,
                   neg_permission_split_same_tag::A,
                   neg_permission_split_same_tag::A> : std::true_type {};

template <>
struct splits_into_authoring_witness<neg_permission_split_same_tag::Whole,
                                     neg_permission_split_same_tag::A,
                                     neg_permission_split_same_tag::A>
    : std::true_type {};

}  // namespace crucible::safety

int main() {
    namespace tags = neg_permission_split_same_tag;
    namespace safe = ::crucible::safety;

    auto whole = safe::mint_permission_root<tags::Whole>();

    // Should FAIL: L == R == tags::A.  all_distinct_tags_v<A, A> is
    // false; the fix-07 static_assert fires.
    auto [a1, a2] = safe::mint_permission_split<tags::A, tags::A>(
        std::move(whole));
    safe::permission_drop(std::move(a1));
    safe::permission_drop(std::move(a2));

    return 0;
}
