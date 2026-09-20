// There must be no nullary rebuild.  A function that reissues a parent
// permission while taking no argument proves nothing about the caller,
// and one existed here: detail::rebuild_parent_after_fork_<Parent>().
//
// It was friended to build the rebuild key itself, so the chain was a
// closed loop whose entry point sat at namespace scope.  Any TU could
// call rebuild_parent_after_fork_<AnyTag>() and obtain a Permission for
// a tag it did not own, with no manifest, no context and no token —
// defeating CSL linearity outright, because a forged parent splits into
// aliasing children over a region the caller never held.
//
// An earlier fixture, test/safety_neg/neg_permission_fork_rebuild_free_
// function_call.cpp, was supposed to prevent exactly this.  It asserted
// that the OLDER name safety::permission_fork_rebuild_ was absent, which
// stayed true while the entry point was renamed into detail:: and left
// callable.  It proved a rename, not a closure.  Hence this fixture
// names the symbol that actually shipped.
//
// The rebuild now belongs to permission_fork_, which consumes the parent
// Permission at the split, so the surrendered token IS the proof.
// Callers who want a parent without a fork go through
// mint_permission_combine_n and present the children.
//
// VIOLATION: a user TU calls the removed nullary door.
//
// Expected diagnostic: the name is not a member of the detail namespace.

#include <foundation/permissions/Permission.h>

namespace {
struct StrangerTag {};
}  // namespace

int main() {
    auto forged = ::foundation::permissions::detail::rebuild_parent_after_fork_<StrangerTag>();
    (void)forged;
    return 0;
}
