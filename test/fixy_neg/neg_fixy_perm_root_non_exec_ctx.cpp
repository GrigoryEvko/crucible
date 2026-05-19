// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-MINT-Perm fixture #1b (FIXY-U-074 HS14 round-out for
// fixy::perm::mint_permission_root):
// the ctx-bound overload's template-parameter constraint
// `::crucible::effects::IsExecCtx Ctx` rejects when the supplied
// `ctx` argument's type does not satisfy the IsExecCtx concept.
//
// Distinct from fixture #1 (neg_fixy_perm_root_no_row_admission):
//   * Fixture #1 passes a valid ExecCtx (HotFgCtx, Row<>) but a Tag
//     whose permission_row carries Row<IO, Block> — fails the
//     CtxAdmitsPermission row-admission predicate (downstream of
//     the IsExecCtx gate, since HotFgCtx satisfies IsExecCtx).
//   * Fixture #1b passes a plain struct that does NOT satisfy
//     IsExecCtx at all — fails the upstream template-parameter
//     constraint, never reaches the row-admission predicate.
//
// Two distinct rejection classes ⇒ HS14 is satisfied for the
// ctx-bound mint_permission_root factory.
//
// Expected diagnostic: IsExecCtx / constraints not satisfied.

#include <crucible/fixy/Perm.h>

namespace neg_fixy_perm_root_non_exec_ctx {

// A plain struct deliberately lacking every ExecCtx machinery —
// no `row_type`, no Cap, no NUMA / heat / residency / workload /
// progress tags.  Trivially fails `effects::IsExecCtx`.
struct NotAnExecCtx {};

struct PureTag {};

}  // namespace neg_fixy_perm_root_non_exec_ctx

int main() {
    namespace tags  = neg_fixy_perm_root_non_exec_ctx;
    namespace fperm = ::crucible::fixy::perm;
    namespace saf   = ::crucible::safety;

    // `mint_permission_root<Tag>(ctx)` requires
    // `::crucible::effects::IsExecCtx Ctx`.  Passing a non-ExecCtx
    // type as `ctx` makes the template constrained-parameter fail
    // to match, BEFORE the body's CtxAdmitsPermission row check.
    auto bad = fperm::mint_permission_root<tags::PureTag>(
        tags::NotAnExecCtx{});
    saf::permission_drop(std::move(bad));
    return 0;
}
