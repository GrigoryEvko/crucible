// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-MINT-Perm fixture #6: mint_permission_combine_n via fixy:: alias
// rejects when `splits_into_pack<Parent, Children...>` has NOT been
// specialized true.
//
// Violation: combine_n is the symmetric inverse of split_n — the
// substrate's `mint_permission_combine_n<Parent, Children...>(...)`
// requires the SAME `splits_into_pack_v<Parent, Children...>` trait.
// Mint each child via mint_permission_root (which has no per-tag
// trait requirement at root), then attempt to combine three children
// into a parent for which NO splits_into_pack specialization exists.
//
// Expected diagnostic: "splits_into_pack" — the static_assert message
// names the missing trait specialization on the parent → pack relation.

#include <crucible/fixy/Perm.h>

namespace neg_fixy_perm_combine_n_undeclared {

struct Parent {};  // no splits_into_pack specialization declared
struct A {};
struct B {};
struct C {};

}  // namespace neg_fixy_perm_combine_n_undeclared

int main() {
    namespace tags  = neg_fixy_perm_combine_n_undeclared;
    namespace fperm = ::crucible::fixy::perm;
    namespace safe  = ::crucible::safety;

    auto a = fperm::mint_permission_root<tags::A>();
    auto b = fperm::mint_permission_root<tags::B>();
    auto c = fperm::mint_permission_root<tags::C>();
    auto whole = fperm::mint_permission_combine_n<tags::Parent>(
        std::move(a), std::move(b), std::move(c));
    safe::permission_drop(std::move(whole));
    return 0;
}
