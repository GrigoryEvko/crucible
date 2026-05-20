// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-M-29 fixture #4: mint_permission_combine_n rejects when
// `splits_into_pack<Parent, Children...>` IS specialized true but the
// companion `splits_into_pack_authoring_witness<Parent, Children...>`
// is NOT.
//
// N-ary symmetric inverse of neg_fixy_perm_split_n_witness_missing.cpp:
// combine_n folds N child permissions back into the parent and the
// substrate's `mint_permission_combine_n<Parent, Children...>(...)`
// reads the SAME splits_into_pack + splits_into_pack_authoring_witness
// pair the split_n minter does (Permission.h:850).  An orphan
// splits_into_pack specialization without the M-29 pack witness must
// fail combine_n's body-level static_assert before it can fold the
// children.
//
// Distinct mismatch class from neg_fixy_perm_combine_n_undeclared.cpp
// (fixture #6): that probes splits_into_pack_v (the trait is absent
// entirely → "splits_into_pack" diagnostic); this probes
// splits_into_pack_authoring_witness_v (the trait is present-but-true
// while its witness companion is absent → distinct rejection point +
// distinct "splits_into_pack_authoring_witness" diagnostic).  Orthogonal
// to the binary witness fixture (#3): pack trait pair vs binary trait
// pair, distinct probe in mint_permission_combine_n's body.
//
// Expected diagnostic: "splits_into_pack_authoring_witness" — the
// assertion message names the missing fixy-M-29 pack witness trait.

#include <crucible/fixy/Perm.h>

namespace neg_fixy_perm_combine_n_witness_missing {

struct Parent {};
struct A {};
struct B {};
struct C {};

}  // namespace neg_fixy_perm_combine_n_witness_missing

// splits_into_pack is specialized true (orphan-bypass scenario), but
// the M-29 companion pack witness is deliberately NOT specialized — so
// mint_permission_combine_n's body-level
// static_assert(splits_into_pack_authoring_witness_v<...>) reddens.
namespace crucible::safety {
template <>
struct splits_into_pack<
    neg_fixy_perm_combine_n_witness_missing::Parent,
    neg_fixy_perm_combine_n_witness_missing::A,
    neg_fixy_perm_combine_n_witness_missing::B,
    neg_fixy_perm_combine_n_witness_missing::C>
    : std::true_type {};
}  // namespace crucible::safety

int main() {
    namespace tags  = neg_fixy_perm_combine_n_witness_missing;
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
