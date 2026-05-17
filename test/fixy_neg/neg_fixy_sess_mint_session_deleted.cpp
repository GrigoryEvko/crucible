// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-MINT-Sess fixture #1: bare mint_session via fixy:: alias.
//
// Violation: `proto::mint_session<Proto>(ctx, resource)` is
// `=delete`d in sessions/SessionMint.h with a diagnostic directing
// callers to `mint_permissioned_session<Proto>(ctx, resource,
// perms...)`.  Routing through `fixy::sess::mint_session` must
// reject identically — proves the deleted overload's diagnostic
// surfaces through the fixy:: namespace path.
//
// Expected diagnostic: "is removed" / "mint_permissioned_session".

#include <crucible/fixy/Sess.h>

namespace neg_fixy_sess_mint_session_deleted {
struct DummyResource {};
}

namespace fsess = ::crucible::fixy::sess;

int main() {
    namespace eff = ::crucible::effects;
    using SendInt = fsess::Send<int, fsess::End>;
    eff::BgCompileCtx ctx{};
    neg_fixy_sess_mint_session_deleted::DummyResource res{};
    // Calling the =deleted overload via fixy:: alias.
    fsess::mint_session<SendInt>(ctx, std::move(res));
    return 0;
}
