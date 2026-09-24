// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// permission_row_t is the public name of a tag's effect row.  A tag with
// no edge and no permission_row member has no row, so the alias refuses
// it rather than read as void.  A consumer that folds the rows of its
// tags then cannot fold an undeclared tag in as an empty row.
//
// Expected diagnostic: the static_assert in permission_row_lookup that
// names the two ways to declare a row.

#include <foundation/permissions/Permission.h>

#include <type_traits>

namespace {
struct Undeclared {};
}  // namespace

int main() {
    using Row = ::foundation::permissions::permission_row_t<Undeclared>;
    return static_cast<int>(std::is_void_v<Row>);
}
