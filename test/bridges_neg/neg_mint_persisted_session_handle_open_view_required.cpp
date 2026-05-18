// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A2-016: HS14 floor for
//   mint_persisted_session(ctx, SessionHandle, cipher, view, ...).
//
// SessionPersistence.h ships an explicit
//   `= delete("[PersistedSession_OpenViewRequired] ...")`
// overload that intercepts callers who supply the (ctx, handle, cipher)
// prefix without the CipherOpenView positional argument.  This fixture
// invokes the deleted overload and the build fails with the embedded
// diagnostic.
//
// Expected diagnostic:
//   "PersistedSession_OpenViewRequired"

#include <crucible/Cipher.h>
#include <crucible/bridges/SessionPersistence.h>
#include <crucible/effects/ExecCtx.h>
#include <crucible/sessions/SessionMint.h>

namespace eff   = ::crucible::effects;
namespace proto = ::crucible::safety::proto;

struct ProbeResource { int value = 0; };

using BgResult = decltype(proto::mint_persisted_session(
    std::declval<eff::BgDrainCtx const&>(),
    std::declval<proto::SessionHandle<proto::End, ProbeResource, proto::Continue>>(),
    std::declval<::crucible::Cipher&>(),
    std::declval<proto::SessionTagId>(),
    std::declval<proto::RoleTagId>(),
    std::declval<proto::RoleTagId>()));

int main() { return 0; }
