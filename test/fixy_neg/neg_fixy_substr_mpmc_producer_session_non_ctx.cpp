// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-U-074g fixture for fixy::substr::mpmc::mint_mpmc_producer_session:
// the `::crucible::effects::IsExecCtx Ctx` template-parameter constraint
// rejects a non-ExecCtx first argument.
//
// Distinct mismatch class from
// neg_fixy_substr_mpmc_producer_session_wrong_handle.cpp: this supplies
// a VALID ProducerHandle but a plain struct that does NOT satisfy
// IsExecCtx as the ctx argument.  Mirrors neg_fixy_perm_root_non_exec_ctx.
//
// Expected diagnostic: IsExecCtx / constraints not satisfied /
// no matching function.

#include <crucible/concurrent/PermissionedMpmcChannel.h>
#include <crucible/fixy/Substr.h>

namespace fsubstr = crucible::fixy::substr;
namespace conc    = crucible::concurrent;

namespace neg_fixy_substr_mpmc_producer_session_non_ctx {
struct UserTag {};
struct NotAnExecCtx {};
}  // namespace neg_fixy_substr_mpmc_producer_session_non_ctx

int main() {
    conc::PermissionedMpmcChannel<int, 16,
        neg_fixy_substr_mpmc_producer_session_non_ctx::UserTag> ch;

    auto producer_opt = ch.producer();

    [[maybe_unused]] auto bad =
        fsubstr::mpmc::mint_mpmc_producer_session<decltype(ch)>(
            neg_fixy_substr_mpmc_producer_session_non_ctx::NotAnExecCtx{},
            *producer_opt);
    return 0;
}
