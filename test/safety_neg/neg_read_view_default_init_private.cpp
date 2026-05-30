// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fix-05 — HS14 fixture #2 of 2 for safety::ReadView<Tag> ctor closure.
//
// Premise: ReadView<Tag> is a read-borrow PROOF token.  A proof you can
// fabricate from nothing is not a proof.  Its default constructor is now
// PRIVATE (mirroring Permission<Tag> / SharedPermission<Tag>); the sole
// public construction paths are the `mint_read_view` chokepoint factory
// (which derives the view from a held parent Permission<Tag>) and the
// copy/move ctors (which duplicate an already-minted view).
//
// Violation: a local declaration with copy-list-init `ReadView<Tag> v{};`
// likewise needs the private default constructor and must be rejected.
//
// Distinct mismatch class from companion fixture
// neg_read_view_default_ctor_private.cpp:
//   * This fixture: DECLARATION copy-list-init `ReadView<Tag> v{};`.
//   * Companion:    braced VALUE-INIT expression `ReadView<Tag>{}`.
//
// Expected diagnostic: "is private within this context".

#include <crucible/permissions/ReadView.h>

namespace {
struct SecretRegionTag {};
}  // namespace

int main() {
    // Default-initializing a view without a parent Permission — rejected.
    crucible::safety::ReadView<SecretRegionTag> v{};
    (void)v;
    return 0;
}
