// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fix-07: mint_permission_fork<Children...> requires the child region
// tags to be PAIRWISE DISTINCT.  Forking with a repeated child tag —
// `mint_permission_fork<A, A>(...)` — would split the parent into two
// `Permission<A>` and hand one to EACH of two jthreads.  Two threads
// mutating the SAME region A is a data race the type system otherwise
// claims is impossible (the whole point of the CSL parallel rule that
// permission_fork encodes).
//
// This exercises a SECOND, distinct mismatch class from
// neg_permission_split_same_tag.cpp: there the binary
// mint_permission_split guard fires; here the N-ary fork guard (and the
// split_n it delegates to) fires.  The diagnostic carries "PAIRWISE
// DISTINCT" / "data race" / "fix-07".

#include <crucible/permissions/PermissionFork.h>

#include <utility>

namespace neg_permission_fork_duplicate_child_tag {

struct Whole {};
struct A {};

}  // namespace neg_permission_fork_duplicate_child_tag

namespace crucible::safety {

template <>
struct splits_into_pack<neg_permission_fork_duplicate_child_tag::Whole,
                        neg_permission_fork_duplicate_child_tag::A,
                        neg_permission_fork_duplicate_child_tag::A>
    : std::true_type {};

template <>
struct splits_into_pack_authoring_witness<
    neg_permission_fork_duplicate_child_tag::Whole,
    neg_permission_fork_duplicate_child_tag::A,
    neg_permission_fork_duplicate_child_tag::A> : std::true_type {};

}  // namespace crucible::safety

int main() {
    namespace tags = neg_permission_fork_duplicate_child_tag;
    namespace eff  = ::crucible::effects;
    namespace safe = ::crucible::safety;

    auto whole = safe::mint_permission_root<tags::Whole>();

    // Should FAIL: two child tags are both tags::A.
    // all_distinct_tags_v<A, A> is false; the fix-07 fork static_assert
    // (and the split_n it delegates to) fires.
    auto rebuilt = safe::mint_permission_fork<tags::A, tags::A>(
        safe::PermissionForkSpawnCtx{},
        std::move(whole),
        [](safe::Permission<tags::A>, safe::PermissionForkSpawnCtx const&) noexcept {},
        [](safe::Permission<tags::A>, safe::PermissionForkSpawnCtx const&) noexcept {});
    safe::permission_drop(std::move(rebuilt));

    return 0;
}
