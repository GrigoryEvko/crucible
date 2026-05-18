// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-079: persisted sessions write Cipher files, so their mint
// context must admit Row<IO, Block>.  HotFgCtx is Row<> and must not
// be able to construct a persistence bridge.

#include <crucible/Cipher.h>  // fixy-A2-014: explicit; SessionPersistence.h no longer pulls Cipher.h
#include <crucible/bridges/SessionPersistence.h>

namespace proto = crucible::safety::proto;

struct Resource {};

int main() {
    auto cipher = crucible::Cipher::open("/tmp/crucible_neg_persist_hot");
    auto view = cipher.mint_open_view();
    crucible::effects::HotFgCtx ctx{};

    [[maybe_unused]] auto h = proto::mint_persisted_session<
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
