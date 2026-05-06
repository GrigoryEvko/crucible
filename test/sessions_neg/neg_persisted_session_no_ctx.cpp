// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-079: mint_persisted_session must be ctx-bound.  The ctx carries
// the compile-time IO+Block authority for Cipher persistence; a
// context-free spelling would bypass the row fence.

#include <crucible/bridges/SessionPersistence.h>

namespace proto = crucible::safety::proto;

struct Resource {};

int main() {
    auto cipher = crucible::Cipher::open("/tmp/crucible_neg_persist_no_ctx");

    [[maybe_unused]] auto h = proto::mint_persisted_session<
        proto::Send<int, proto::End>>(
            cipher,
            Resource{},
            proto::SessionTagId{1},
            proto::RoleTagId{1},
            proto::RoleTagId{2});
    return 0;
}
