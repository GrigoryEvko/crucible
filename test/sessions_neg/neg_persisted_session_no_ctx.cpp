// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-079: mint_persisted_session must be ctx-bound.  The ctx carries
// the compile-time IO+Block authority for Cipher persistence; a
// context-free spelling would bypass the row fence.

#include <crucible/Cipher.h>  // fixy-A2-014: explicit; SessionPersistence.h no longer pulls Cipher.h
#include <crucible/bridges/SessionPersistence.h>

// FIXY-V-031: Cipher::open() now takes Path<source::External>.
using CipherRoot = crucible::fixy::wrap::Path<
    crucible::fixy::tags::source::External>;

namespace proto = crucible::safety::proto;

struct Resource {};

int main() {
    auto cipher = crucible::Cipher::open(CipherRoot{"/tmp/crucible_neg_persist_no_ctx"});
    auto view = cipher.mint_open_view();

    [[maybe_unused]] auto h = proto::mint_persisted_session<
        proto::Send<int, proto::End>>(
            cipher,
            view,
            Resource{},
            proto::SessionTagId{1},
            proto::RoleTagId{1},
            proto::RoleTagId{2});
    return 0;
}
