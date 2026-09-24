// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A tag declares its row with a permission_row member, and the member
// must name an effect row.  A member that names any other type is
// refused where the row is read, so no consumer of permission_row_t
// sees a type that is not a row.
//
// Expected diagnostic: the static_assert in permission_row_lookup that
// asks for a foundation::effects::Row.

#include <foundation/permissions/Permission.h>

#include <type_traits>

namespace {
struct RowIsAnInt {
    using permission_row = int;
};
}  // namespace

int main() {
    using Row = ::foundation::permissions::permission_row_t<RowIsAnInt>;
    return static_cast<int>(std::is_same_v<Row, int>);
}
