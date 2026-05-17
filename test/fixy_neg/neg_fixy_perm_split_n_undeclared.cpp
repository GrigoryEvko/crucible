// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-MINT-Perm fixture #5: mint_permission_split_n via fixy:: alias
// rejects when `splits_into_pack<In, Children...>` has NOT been
// specialized true.
//
// Violation: the N-ary frame-rule split requires
// `splits_into_pack_v<In, Children...>` to be true.  The substrate's
// static_assert carries the per-arity message; the fixy:: alias
// preserves it.
//
// Expected diagnostic: "splits_into_pack" — the static_assert message
// names the missing trait specialization on the parent → pack relation.

#include <crucible/fixy/Perm.h>

namespace neg_fixy_perm_split_n_undeclared {

struct Whole {};
struct A {};
struct B {};
struct C {};

// Note: NO splits_into_pack specialization declared.  Substrate must
// reject the 3-way split of Whole → (A, B, C).

}  // namespace neg_fixy_perm_split_n_undeclared

int main() {
    namespace tags  = neg_fixy_perm_split_n_undeclared;
    namespace fperm = ::crucible::fixy::perm;
    namespace safe  = ::crucible::safety;

    auto whole = fperm::mint_permission_root<tags::Whole>();
    auto [a, b, c] = fperm::mint_permission_split_n<tags::A, tags::B, tags::C>(
        std::move(whole));
    safe::permission_drop(std::move(a));
    safe::permission_drop(std::move(b));
    safe::permission_drop(std::move(c));
    return 0;
}
