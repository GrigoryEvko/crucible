// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// proto::mint_session<Proto>(resource), the overload without a context, is
// deleted in sessions/SessionMint.h.  Its deletion message sends the caller
// to mint_permissioned_session<Proto>(ctx, resource, perms...).  The
// overload that takes a context has its own fixture.
//
// Expected diagnostic: "is removed" / "mint_permissioned_session".

#include <crucible/sessions/SessionMint.h>

#include <utility>

namespace neg_fixy_sess_mint_session_bare_deleted {
struct DummyResource {};
}  // namespace neg_fixy_sess_mint_session_bare_deleted

namespace proto = ::crucible::safety::proto;

int main() {
    using SendInt = proto::Send<int, proto::End>;
    neg_fixy_sess_mint_session_bare_deleted::DummyResource res{};
    proto::mint_session<SendInt>(std::move(res));
    return 0;
}
