// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-M-29 fixture #3: mint_permission_combine rejects when
// `splits_into<In, L, R>` IS specialized true but the companion
// `splits_into_authoring_witness<In, L, R>` is NOT.
//
// Symmetric inverse of neg_fixy_perm_split_witness_missing.cpp: combine
// is the merge direction (P, Q ⊢ P * Q) and the substrate's
// `mint_permission_combine<In>(Permission<L>&&, Permission<R>&&)` reads
// the SAME splits_into + splits_into_authoring_witness pair the split
// minter does (Permission.h:751-753).  An orphan splits_into
// specialization without the M-29 witness must fail combine's
// body-level static_assert before it can fold two child permissions
// back into the parent.
//
// Distinct mismatch class from neg_fixy_perm_combine_undeclared.cpp
// (fixture #4): that probes splits_into_v (the trait is absent
// entirely → "splits_into" diagnostic); this probes
// splits_into_authoring_witness_v (the trait is present-but-true while
// its witness companion is absent → distinct rejection point + distinct
// "splits_into_authoring_witness" diagnostic in mint_permission_combine's
// body).
//
// Expected diagnostic: "splits_into_authoring_witness" — the assertion
// message names the missing fixy-M-29 witness trait.

#include <crucible/fixy/Perm.h>

namespace neg_fixy_perm_combine_witness_missing {

struct Parent {};
struct Left {};
struct Right {};

}  // namespace neg_fixy_perm_combine_witness_missing

// splits_into is specialized true (simulating the foreign-TU
// orphan-bypass scenario the grep gate cannot catch in vendored or
// non-CI builds).  But the M-29 companion witness is deliberately NOT
// specialized — so mint_permission_combine's body-level
// static_assert(splits_into_authoring_witness_v<...>) reddens.
namespace crucible::safety {
template <>
struct splits_into<
    neg_fixy_perm_combine_witness_missing::Parent,
    neg_fixy_perm_combine_witness_missing::Left,
    neg_fixy_perm_combine_witness_missing::Right>
    : std::true_type {};
}  // namespace crucible::safety

int main() {
    namespace tags  = neg_fixy_perm_combine_witness_missing;
    namespace fperm = ::crucible::fixy::perm;
    namespace safe  = ::crucible::safety;

    auto l = fperm::mint_permission_root<tags::Left>();
    auto r = fperm::mint_permission_root<tags::Right>();
    auto whole = fperm::mint_permission_combine<tags::Parent>(
        std::move(l), std::move(r));
    safe::permission_drop(std::move(whole));
    return 0;
}
