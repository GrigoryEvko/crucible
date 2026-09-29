// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A permission tag is a phantom marker: an empty class that is not a
// union.  A tag with a data member is a class, so a check for a class
// type alone admits it, and a token over it carries state that no mint
// wrote.  The emptiness half of PermissionTag is what refuses it.
//
// Expected diagnostic: the static_assert in Permission that asks for
// PermissionTag<Tag>.

#include <foundation/permissions/Permission.h>

namespace {
struct Stateful {
    using permission_row = ::foundation::effects::Row<>;
    int count = 0;
};
}  // namespace

int main() { return static_cast<int>(sizeof(::foundation::permissions::Permission<Stateful>)); }
