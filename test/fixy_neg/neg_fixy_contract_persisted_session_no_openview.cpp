// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-MINT-Contract fixture: mint_persisted_session via
// fixy::contract::cipher rejects when invoked without a Cipher::OpenView.
//
// Violation: a deleted overload of mint_persisted_session catches the
// missing-OpenView form with a structured tag:
//   = delete("[PersistedSession_OpenViewRequired] "
//            "mint_persisted_session<Proto> requires Cipher::OpenView at "
//            "the mint boundary; pass cipher.mint_open_view() explicitly.");
// Routing through the fixy:: alias must reject identically.
//
// Expected diagnostic: PersistedSession_OpenViewRequired / deleted function.

#include <crucible/fixy/Contract.h>

// FIXY-V-031: Cipher::open() now takes Path<source::External>.
using CipherRoot = crucible::fixy::wrap::Path<
    crucible::fixy::tags::source::External>;

namespace fcipher = ::crucible::fixy::contract::cipher;
namespace proto   = ::crucible::safety::proto;
namespace eff     = ::crucible::effects;

struct Resource {};

int main() {
    auto cipher = ::crucible::Cipher::open(
        CipherRoot{"/tmp/crucible_neg_fixy_persist_no_openview"});
    eff::BgCompileCtx ctx{};

    // Should FAIL: deleted overload — Ctx present, Cipher present,
    // but no OpenView passed.
    [[maybe_unused]] auto h = fcipher::mint_persisted_session<
        proto::Send<int, proto::End>>(
            ctx,
            cipher,
            Resource{},
            proto::SessionTagId{1},
            proto::RoleTagId{1},
            proto::RoleTagId{2});
    return 0;
}
