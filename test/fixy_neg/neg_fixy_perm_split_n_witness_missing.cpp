// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-M-29 fixture #2: mint_permission_split_n rejects when
// `splits_into_pack<In, Children...>` IS specialized true but the
// companion `splits_into_pack_authoring_witness<In, Children...>` is
// NOT.
//
// N-ary counterpart to fixy-M-29 fixture #1
// (neg_fixy_perm_split_witness_missing.cpp).  The N-ary mint surface
// reads splits_into_pack and ALSO requires the pack-witness; orphan
// N-ary specializations without the witness fail the body-level
// static_assert before the mint can produce any child permission.
//
// Distinct mismatch class from fixture #1: the pack form is exercised
// via `mint_permission_split_n` which probes `splits_into_pack_v` +
// `splits_into_pack_authoring_witness_v`, while the binary form
// probes `splits_into_v` + `splits_into_authoring_witness_v` —
// orthogonal trait pair, distinct rejection point in
// `mint_permission_split_n`'s body (~Permission.h:796).
//
// Expected diagnostic: "splits_into_pack_authoring_witness" — the
// assertion message names the missing fixy-M-29 pack witness trait.

#include <crucible/fixy/Perm.h>

namespace neg_fixy_perm_split_n_witness_missing {

struct Whole {};
struct ChildA {};
struct ChildB {};
struct ChildC {};

}  // namespace neg_fixy_perm_split_n_witness_missing

// `splits_into_pack` is specialized true (simulating the foreign-TU
// orphan-bypass scenario the grep gate cannot catch in vendored or
// non-CI builds).  But the M-29 companion pack witness is deliberately
// NOT specialized — so `mint_permission_split_n`'s body-level
// static_assert(splits_into_pack_authoring_witness_v<...>) reddens.
namespace crucible::safety {
template <>
struct splits_into_pack<
    neg_fixy_perm_split_n_witness_missing::Whole,
    neg_fixy_perm_split_n_witness_missing::ChildA,
    neg_fixy_perm_split_n_witness_missing::ChildB,
    neg_fixy_perm_split_n_witness_missing::ChildC>
    : std::true_type {};
}  // namespace crucible::safety

int main() {
    namespace tags  = neg_fixy_perm_split_n_witness_missing;
    namespace fperm = ::crucible::fixy::perm;
    namespace safe  = ::crucible::safety;

    auto whole = fperm::mint_permission_root<tags::Whole>();
    auto [a, b, c] = fperm::mint_permission_split_n<
        tags::ChildA, tags::ChildB, tags::ChildC>(std::move(whole));
    safe::permission_drop(std::move(a));
    safe::permission_drop(std::move(b));
    safe::permission_drop(std::move(c));
    return 0;
}
