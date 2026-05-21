// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A2-006 — companion to neg_persisted_session_no_open_view.cpp
// for the PSH overload.  The PSH wrap-variant mint requires a
// Cipher::OpenView at the boundary just like the bare-handle variant;
// the `= delete` companion fires when callers forget to pass one and
// the substitution falls through to the deleted overload taking a
// bare Cipher&.
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "PersistedSession_OpenViewRequired" / "use of deleted function".

#include <crucible/Cipher.h>  // fixy-A2-014: explicit; SessionPersistence.h no longer pulls Cipher.h
#include <crucible/bridges/SessionPersistence.h>
#include <crucible/sessions/SessionMint.h>

// FIXY-V-031: Cipher::open() now takes Path<source::External>.
using CipherRoot = crucible::fixy::wrap::Path<
    crucible::fixy::tags::source::External>;

namespace proto = crucible::safety::proto;
namespace eff = crucible::effects;

struct Resource {};

int main() {
    auto cipher = crucible::Cipher::open(
        CipherRoot{"/tmp/crucible_neg_persist_psh_no_view"});
    eff::TestRunnerCtx ctx{};

    auto psh = proto::mint_permissioned_session<
        proto::Send<int, proto::End>>(ctx, Resource{});

    // PSH overload deleted companion fires: no Cipher::OpenView at the
    // mint boundary; pass cipher.mint_open_view() explicitly.
    [[maybe_unused]] auto h = proto::mint_persisted_session(
        ctx,
        std::move(psh),
        cipher,
        proto::SessionTagId{1},
        proto::RoleTagId{1},
        proto::RoleTagId{2});
    return 0;
}
