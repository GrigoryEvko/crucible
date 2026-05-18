// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A2-016: HS14 floor for
//   mint_persisted_session<Proto>(ctx, cipher, view, resource, ...).
//
// SessionPersistence.h ships an explicit
//   `= delete("[PersistedSession_CtxRequired] ...")`
// overload that intercepts callers who omit the ctx parameter.  This
// fixture invokes the deleted overload by passing the cipher/view/...
// pack WITHOUT a leading ctx; the call resolves to the delete-tagged
// overload and the build fails with the embedded diagnostic.
//
// Expected diagnostic:
//   "PersistedSession_CtxRequired"

#include <crucible/Cipher.h>
#include <crucible/bridges/SessionPersistence.h>
#include <crucible/sessions/SessionMint.h>

namespace proto = ::crucible::safety::proto;

struct ProbeResource { int value = 0; };

using NoCtxResult = decltype(proto::mint_persisted_session<proto::End>(
    std::declval<::crucible::Cipher&>(),
    std::declval<::crucible::CipherOpenView const&>(),
    std::declval<ProbeResource>(),
    std::declval<proto::SessionTagId>(),
    std::declval<proto::RoleTagId>(),
    std::declval<proto::RoleTagId>()));

int main() { return 0; }
