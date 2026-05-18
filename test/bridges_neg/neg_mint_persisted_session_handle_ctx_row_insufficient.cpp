// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A2-016: HS14 floor for
//   mint_persisted_session(ctx, SessionHandle, cipher, view, ...).
//
// Like neg_mint_persisted_session_ctx_row_insufficient.cpp but on the
// (ctx, handle, cipher, view, ...) overload.  HotFgCtx's empty row does
// not admit IO or Block; the live overload's requires-clause rejects
// the call.
//
// Expected diagnostic:
//   "constraints not satisfied"
//   or "CtxAdmits"

#include <crucible/Cipher.h>
#include <crucible/bridges/SessionPersistence.h>
#include <crucible/effects/ExecCtx.h>
#include <crucible/sessions/SessionMint.h>

namespace eff   = ::crucible::effects;
namespace proto = ::crucible::safety::proto;

struct ProbeResource { int value = 0; };

using HotHandleResult = decltype(proto::mint_persisted_session(
    std::declval<eff::HotFgCtx const&>(),
    std::declval<proto::SessionHandle<proto::End, ProbeResource, proto::Continue>>(),
    std::declval<::crucible::Cipher&>(),
    std::declval<::crucible::CipherOpenView const&>(),
    std::declval<proto::SessionTagId>(),
    std::declval<proto::RoleTagId>(),
    std::declval<proto::RoleTagId>()));

int main() { return 0; }
