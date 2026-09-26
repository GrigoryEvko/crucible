// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// proto::mint_session<Proto>(ctx, resource) is deleted in
// sessions/_SessionMint.h.  Its deletion message sends the caller to
// mint_permissioned_session<Proto>(ctx, resource, perms...).
//
// Expected diagnostic: "is removed" / "mint_permissioned_session".

#include <crucible/effects/_ExecCtx.h>
#include <crucible/sessions/_SessionMint.h>

#include <utility>

namespace neg_fixy_sess_mint_session_deleted {
struct DummyResource {};
}  // namespace neg_fixy_sess_mint_session_deleted

namespace proto = ::crucible::safety::proto;

int main() {
    namespace eff = ::crucible::effects;
    using SendInt = proto::Send<int, proto::End>;
    eff::BgCompileCtx ctx{::crucible::effects::testing::bg()};
    neg_fixy_sess_mint_session_deleted::DummyResource res{};
    proto::mint_session<SendInt>(ctx, std::move(res));
    return 0;
}
