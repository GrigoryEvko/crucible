// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A1-011 HS14 fixture #2: pointer Tag rejected.
//
// Pointers and references are the SECOND silently-admitted shape under
// the pre-fix surface.  A reader who saw `Permission<MyTag*>` would
// reasonably believe the token guards *the storage pointed to by the
// pointer's identity*; in fact it guarded nothing, because no CSL
// specialization keys off a pointer type and the underlying region is
// addressed by `Tag*` value, not by `Tag*` type-identity.  The phantom
// is per-type, not per-pointee.
//
// fixy-A1-011's `PermissionTag` concept rejects pointers (a pointer is
// not `is_class_v`), references (also not `is_class_v`), and enums (a
// scoped enum is `is_class_v` *false* — only structs/unions/classes
// satisfy `is_class_v`).  Unions additionally fail `is_empty_v`'s
// implicit non-union requirement (`is_empty_v<U> == false` for any
// union U per [meta.unary.prop]).
//
// VIOLATION: a user TU tries `Permission<MyTag*>` (pointer-to-class).
// `is_class_v<MyTag*>` is false because pointer-to-class is itself a
// pointer type, not a class type.  The static_assert fires.
//
// Expected diagnostic: "static assertion failed", "Tag must be an
// empty non-union class type", "PermissionTag", or equivalent.

#include <crucible/permissions/Permission.h>

namespace neg_permission_pointer_tag {

struct MyTag {};  // Empty class — would satisfy PermissionTag.

}  // namespace neg_permission_pointer_tag

int main() {
    namespace safe = ::crucible::safety;
    using neg_permission_pointer_tag::MyTag;

    // VIOLATION: pointer-to-class is not a class type.  Pre-fix this
    // compiled silently, carrying zero guarantee.
    auto rejected = safe::mint_permission_root<MyTag*>();
    safe::permission_drop(std::move(rejected));
    return 0;
}
