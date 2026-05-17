// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-MINT-Contract fixture: mint_persisted_session via
// fixy::contract::cipher rejects when invoked without an execution
// context.
//
// Violation: a deleted overload of mint_persisted_session catches the
// no-Ctx form with a structured tag:
//   = delete("[PersistedSession_CtxRequired] mint_persisted_session<Proto> "
//            "requires an execution context whose row admits Cipher "
//            "persistence effects.");
// Routing through the fixy:: alias must reject identically.
//
// Expected diagnostic: PersistedSession_CtxRequired / deleted function.

#include <crucible/fixy/Contract.h>

namespace fcipher = ::crucible::fixy::contract::cipher;
namespace proto   = ::crucible::safety::proto;

struct Resource {};

int main() {
    auto cipher = ::crucible::Cipher::open(
        "/tmp/crucible_neg_fixy_persist_no_ctx");
    auto view = cipher.mint_open_view();

    // Should FAIL: deleted overload — no Ctx parameter at all.
    [[maybe_unused]] auto h = fcipher::mint_persisted_session<
        proto::Send<int, proto::End>>(
            cipher,
            view,
            Resource{},
            proto::SessionTagId{1},
            proto::RoleTagId{1},
            proto::RoleTagId{2});
    return 0;
}
