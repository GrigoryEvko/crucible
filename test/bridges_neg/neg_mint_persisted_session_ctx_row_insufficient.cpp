// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A2-016: HS14 floor for
//   mint_persisted_session<Proto>(ctx, cipher, view, resource, ...).
//
// The factory's requires-clause is
//   requires effects::CtxAdmits<Ctx, CipherSessionEventPersistenceRow>
// where CipherSessionEventPersistenceRow = Row<IO, Block>.  HotFgCtx
// is the canonical hot-foreground ctx whose row is empty Row<>, so it
// does NOT admit IO or Block effects.  Calling with HotFgCtx triggers
// constraint failure on the live (non-delete) overload, surfacing the
// Cipher-row requirement at the mint boundary.
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

using HotResult = decltype(proto::mint_persisted_session<proto::End>(
    std::declval<eff::HotFgCtx const&>(),
    std::declval<::crucible::Cipher&>(),
    std::declval<::crucible::CipherOpenView const&>(),
    std::declval<ProbeResource>(),
    std::declval<proto::SessionTagId>(),
    std::declval<proto::RoleTagId>(),
    std::declval<proto::RoleTagId>()));

int main() { return 0; }
