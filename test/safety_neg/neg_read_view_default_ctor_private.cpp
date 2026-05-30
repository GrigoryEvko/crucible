// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fix-05 — HS14 fixture #1 of 2 for safety::ReadView<Tag> ctor closure.
//
// Premise: ReadView<Tag> is a read-borrow PROOF token.  A proof you can
// fabricate from nothing is not a proof.  Its default constructor is now
// PRIVATE (mirroring Permission<Tag> / SharedPermission<Tag>); the sole
// public construction paths are the `mint_read_view` chokepoint factory
// (which derives the view from a held parent Permission<Tag>) and the
// copy/move ctors (which duplicate an already-minted view).
//
// Violation: direct value-initialization `ReadView<Tag>{}` bypasses the
// mint factory and tries to call the private default constructor.
//
// Distinct mismatch class from companion fixture
// neg_read_view_default_init_private.cpp:
//   * This fixture: braced VALUE-INIT expression `ReadView<Tag>{}`.
//   * Companion:    DECLARATION copy-list-init `ReadView<Tag> v{};`.
//
// Expected diagnostic: "is private within this context".

#include <crucible/permissions/ReadView.h>

namespace {
struct SecretRegionTag {};
}  // namespace

int main() {
    // Fabricating a read-borrow proof from nothing — must be rejected.
    auto bad = crucible::safety::ReadView<SecretRegionTag>{};
    (void)bad;
    return 0;
}
