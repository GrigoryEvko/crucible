// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-M-29 fixture #1: mint_permission_split rejects when
// `splits_into<In, L, R>` IS specialized true but the companion
// `splits_into_authoring_witness<In, L, R>` is NOT.
//
// Pre-fixy-M-29 this TU would have minted two child permissions from
// a parent permission by simply declaring `splits_into` true; the
// orphan-rule discipline was review-only via fixy-A1-018's grep gate
// (which a foreign TU could bypass by vendoring or `--no-verify`).
// fixy-M-29 adds the TYPE-LEVEL companion: even if a foreign TU
// somehow lands a `splits_into` specialization, `mint_permission_split`
// requires `splits_into_authoring_witness` AS WELL, and an orphan
// specialization without the witness fails the body-level
// static_assert before the mint can produce a child permission.
//
// Violation: this TU specializes `splits_into` (the orphan-rule
// surface) but NOT `splits_into_authoring_witness` (the M-29 closure).
// `mint_permission_split` invocation must fire the static_assert
// naming the missing witness trait.
//
// Expected diagnostic: "splits_into_authoring_witness" — the assertion
// message names the missing fixy-M-29 witness trait.

#include <crucible/fixy/Perm.h>

namespace neg_fixy_perm_split_witness_missing {

struct Whole {};
struct Left {};
struct Right {};

}  // namespace neg_fixy_perm_split_witness_missing

// `splits_into` is specialized true (simulating the foreign-TU
// orphan-bypass scenario the grep gate cannot catch in vendored or
// non-CI builds).  But the M-29 companion witness is deliberately
// NOT specialized — so `mint_permission_split`'s body-level
// static_assert(splits_into_authoring_witness_v<...>) reddens.
namespace crucible::safety {
template <>
struct splits_into<
    neg_fixy_perm_split_witness_missing::Whole,
    neg_fixy_perm_split_witness_missing::Left,
    neg_fixy_perm_split_witness_missing::Right>
    : std::true_type {};
}  // namespace crucible::safety

int main() {
    namespace tags  = neg_fixy_perm_split_witness_missing;
    namespace fperm = ::crucible::fixy::perm;
    namespace safe  = ::crucible::safety;

    auto whole = fperm::mint_permission_root<tags::Whole>();
    auto [l, r] = fperm::mint_permission_split<tags::Left, tags::Right>(
        std::move(whole));
    safe::permission_drop(std::move(l));
    safe::permission_drop(std::move(r));
    return 0;
}
