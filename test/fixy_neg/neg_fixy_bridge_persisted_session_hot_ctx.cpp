// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-MINT-Bridge fixture #1: mint_persisted_session via fixy::
// alias rejects when ctx is HotFgCtx (Row<>) instead of a
// row-containing-IO/Block ctx.
//
// Violation: persisted sessions write Cipher files (IO + Block).
// Routing through `fixy::bridge::mint_persisted_session` must
// reject identically to the substrate.
//
// Expected diagnostic: concept / requires-clause failure on the
// IO+Block row admission.

#include <crucible/fixy/Bridge.h>

namespace fbridge = crucible::fixy::bridge;
namespace proto   = crucible::safety::proto;

struct Resource {};

int main() {
    auto cipher = crucible::Cipher::open("/tmp/crucible_neg_fixy_persist_hot");
    auto view = cipher.mint_open_view();
    crucible::effects::HotFgCtx ctx{};

    [[maybe_unused]] auto h = fbridge::mint_persisted_session<
        proto::Send<int, proto::End>>(
            ctx,
            cipher,
            view,
            Resource{},
            proto::SessionTagId{1},
            proto::RoleTagId{1},
            proto::RoleTagId{2});
    return 0;
}
