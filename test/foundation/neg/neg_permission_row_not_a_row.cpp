// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A tag declares its row with a permission_row member, and the member
// must name an effect row.  A member that names any other type is
// refused where the row is read, so no consumer of permission_row_t
// sees a type that is not a row.
//
// Expected diagnostic: the refusal in permission_row_of that asks for a
// foundation::effects::Row.

#include <foundation/permissions/Permission.h>

namespace {
struct RowIsAnInt {
    using permission_row = int;
};
}  // namespace

// The requirement is the one use of the row, so the refusal is the only
// error.
static_assert(requires { typename ::foundation::permissions::permission_row_t<RowIsAnInt>; });

int main() { return 0; }
