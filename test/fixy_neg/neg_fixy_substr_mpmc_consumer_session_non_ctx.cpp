// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// MPMC session mint fixture for
// safety::proto::mpmc_channel_session::mint_mpmc_consumer_session:
// the `::crucible::effects::IsExecCtx Ctx` template-parameter constraint
// rejects a non-ExecCtx first argument.
//
// Distinct mismatch class from
// neg_fixy_substr_mpmc_consumer_session_wrong_handle.cpp: this supplies
// the CORRECT ConsumerHandle but a plain struct that does NOT satisfy
// IsExecCtx as the ctx argument.  Mirrors neg_fixy_perm_root_non_exec_ctx.
//
// Expected diagnostic: IsExecCtx / constraints not satisfied /
// no matching function.

#include <crucible/concurrent/PermissionedMpmcChannel.h>
#include <crucible/sessions/MpmcChannelSession.h>

namespace fmpmc = ::crucible::safety::proto::mpmc_channel_session;
namespace conc = crucible::concurrent;

namespace neg_fixy_substr_mpmc_consumer_session_non_ctx {
struct UserTag {};
struct NotAnExecCtx {};
}  // namespace neg_fixy_substr_mpmc_consumer_session_non_ctx

int main() {
    conc::PermissionedMpmcChannel<int, 16, neg_fixy_substr_mpmc_consumer_session_non_ctx::UserTag> ch;

    auto consumer_opt = ch.consumer();

    [[maybe_unused]] auto bad = fmpmc::mint_mpmc_consumer_session<decltype(ch)>(
        neg_fixy_substr_mpmc_consumer_session_non_ctx::NotAnExecCtx{}, *consumer_opt);
    return 0;
}
