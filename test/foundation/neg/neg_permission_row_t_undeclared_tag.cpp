// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// permission_row_t is the public name of a tag's effect row.  A tag with
// no edge and no permission_row member has no row, so the alias refuses
// it rather than read as void.  A consumer that folds the rows of its
// tags then cannot fold an undeclared tag in as an empty row.
//
// Expected diagnostic: the refusal in permission_row_of that names the
// two ways to declare a row.

#include <foundation/permissions/Permission.h>

namespace {
struct Undeclared {};
}  // namespace

// The requirement is the one use of the row, so the refusal is the only
// error.
static_assert(requires { typename ::foundation::permissions::permission_row_t<Undeclared>; });

int main() { return 0; }
