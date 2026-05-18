// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #2 of 2 for fixy-A1-028 (#1565 / fixy-L-01 #1517):
// `Linear<volatile SharedPermission<Tag>>` doc-vs-code drift closure.
//
// Premise: A1-004 (#1546) added the `is_already_linear<T>` trait
// specialization to reject `Linear<SharedPermission<Tag>>` stacking
// per CLAUDE.md §XVI.  The original specialization matched only the
// bare `SharedPermission<Tag>` shape — cv-qualified variants slipped
// past because the partial-specialization match fails on cv T.
// A1-028 re-routes the public trait through `std::remove_cvref_t<T>`
// so cv-qualified and reference-qualified variants collapse to the
// bare specialization before the lookup.  Docs now match code.
//
// Distinct mismatch class from companion fixture
// neg_linear_of_const_permission_redundant.cpp:
//   * Companion:    Linear<const Permission<Tag>>           (Permission, const branch)
//   * This fixture: Linear<volatile SharedPermission<Tag>>  (SharedPermission, volatile branch)
// Both must reach the same static_assert via the remove_cvref_t
// indirection — proves the strip applies to BOTH trait
// specializations AND BOTH cv axes (Permission × const, SharedPermission
// × volatile is a 2-D coverage of the closure).
//
// Substring "redundant" pins the diagnostic — Linear.h's static_assert
// message leads with "Linear<Permission<Tag>> / Linear<SharedPermission
// <Tag>> is redundant: ...".

#include <crucible/safety/Linear.h>
#include <crucible/permissions/Permission.h>

namespace {
struct MyTag {};
}  // namespace

int main() {
    using crucible::safety::Linear;
    using crucible::safety::SharedPermission;

    // Should FAIL: Linear<volatile SharedPermission<MyTag>> trips
    // the is_already_linear_v<volatile SharedPermission<MyTag>>
    // static_assert (via remove_cvref_t reduction to
    // SharedPermission<MyTag>).
    using BadType = Linear<volatile SharedPermission<MyTag>>;
    static_cast<void>(sizeof(BadType));
    return 0;
}
