// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A1-011 HS14 fixture #3: stateful (non-empty) Tag rejected.
//
// The phantom-Tag convention requires that Tag carries NO state of
// its own — `sizeof(Permission<Tag>)` should collapse to the empty-
// class minimum 1 byte, and `[[no_unique_address]]` member packing
// should collapse it to 0 bytes via EBO.  A Tag with a data member
// breaks this: now `sizeof(Permission<Tag>) >= sizeof(MemberT)`, EBO
// no longer kicks in, the "free" linearity proof is no longer free,
// and a Tag instance physically exists and can be inspected — which
// defeats the phantom-only discipline.  Worse, the data carried by
// the Tag is ALSO part of the Permission's identity in a subtle way
// readers don't expect.
//
// fixy-A1-011's `PermissionTag` concept requires `is_empty_v<T>` —
// satisfied only when the class has no non-static data members, no
// non-empty bases, and no virtual functions / bases.  Federation tags
// like `tag::FederatedPeer<Org>` carry only a `using org_type = Org;`
// typedef (not a data member), so `is_empty_v` is still true and they
// remain admissible.  A struct with an `int payload` data member is
// rejected.
//
// VIOLATION: a user TU tries `Permission<StatefulTag>` where
// `StatefulTag` has a data member.  `is_empty_v<StatefulTag>` is false.
// The static_assert fires.
//
// Expected diagnostic: "static assertion failed", "Tag must be an
// empty non-union class type", "PermissionTag", or equivalent.

#include <crucible/permissions/Permission.h>

namespace neg_permission_non_empty_tag {

// Stateful: carries a data member.  `is_empty_v<StatefulTag>` is false.
struct StatefulTag {
    int payload = 0;
};

}  // namespace neg_permission_non_empty_tag

int main() {
    namespace safe = ::crucible::safety;
    using neg_permission_non_empty_tag::StatefulTag;

    // VIOLATION: StatefulTag has a data member; not empty.
    auto rejected = safe::mint_permission_root<StatefulTag>();
    safe::permission_drop(std::move(rejected));
    return 0;
}
