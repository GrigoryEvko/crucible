// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #1 of 2 for fixy-A1-028 (#1565 / fixy-L-01 #1517):
// `Linear<const Permission<Tag>>` doc-vs-code drift closure.
//
// Premise: A1-004 (#1546) added the `is_already_linear<T>` trait
// specialization to reject `Linear<Permission<Tag>>` stacking per
// CLAUDE.md §XVI.  The original specialization matched only the
// bare `Permission<Tag>` shape — `Linear<const Permission<Tag>>` and
// `Linear<volatile Permission<Tag>>` slipped past because the
// partial-specialization match fails on cv-qualified T.  A1-028
// re-routes the public trait through `std::remove_cvref_t<T>` so
// cv-qualified and reference-qualified variants collapse to the
// bare specialization before the lookup.  Docs now match code.
//
// Distinct mismatch class from companion fixture
// neg_linear_of_volatile_permission_redundant.cpp:
//   * This fixture: Linear<const Permission<Tag>>     (const-qualified branch)
//   * Companion:    Linear<volatile Permission<Tag>>  (volatile-qualified branch)
// Both must reach the same static_assert via the remove_cvref_t
// indirection — proves the strip is applied to BOTH cv axes.
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
    using crucible::safety::Permission;

    // Should FAIL: Linear<const Permission<MyTag>> trips the
    // is_already_linear_v<const Permission<MyTag>> static_assert
    // (via remove_cvref_t reduction to Permission<MyTag>).
    using BadType = Linear<const Permission<MyTag>>;
    static_cast<void>(sizeof(BadType));
    return 0;
}
