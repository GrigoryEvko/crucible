// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A2-006 — companion to neg_persisted_session_hot_ctx.cpp for the
// PSH overload of mint_persisted_session.  The bare SessionHandle
// overload already enforces CtxAdmits<Ctx, persist_session_events_
// required_row>; the new PSH overload (fixy-A2-006) ships the same
// gate so permissioned channels can't sneak past the row check.
//
// Persisted sessions write Cipher cold-tier files, so the mint
// context must admit Row<IO, Block>.  HotFgCtx is Row<> and must fail
// the CtxAdmits constraint on the PSH path identically to the bare-
// handle path.
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "constraints not satisfied" / "template constraint failure" /
//   "associated constraints" / "CtxAdmits".

#include <crucible/Cipher.h>  // fixy-A2-014: explicit; SessionPersistence.h no longer pulls Cipher.h
#include <crucible/bridges/SessionPersistence.h>
#include <crucible/sessions/SessionMint.h>

namespace proto = crucible::safety::proto;
namespace eff = crucible::effects;

struct Resource {};

int main() {
    auto cipher = crucible::Cipher::open("/tmp/crucible_neg_persist_psh_hot");
    auto view = cipher.mint_open_view();
    eff::HotFgCtx ctx{};

    // First mint a valid PSH (this uses the same HotFgCtx but PSH
    // construction itself doesn't gate on Cipher row).
    auto psh = proto::mint_permissioned_session<
        proto::Send<int, proto::End>>(ctx, Resource{});

    // PSH overload of mint_persisted_session must reject HotFgCtx
    // because Cipher persistence requires Row<IO, Block>.
    [[maybe_unused]] auto h = proto::mint_persisted_session(
        ctx,
        std::move(psh),
        cipher,
        view,
        proto::SessionTagId{1},
        proto::RoleTagId{1},
        proto::RoleTagId{2});
    return 0;
}
