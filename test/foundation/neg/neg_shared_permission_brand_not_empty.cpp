// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A share proof carries the brand of the exclusive it came from, and a
// brand carries no state.  A brand with a data member would give the
// proof bytes that no share wrote.  The type is spellable although no
// mint builds it, and the IsBrand check in the class body of
// SharedPermission is what refuses the spelling.
//
// Expected diagnostic: the static_assert in SharedPermission that asks
// for IsBrand<Brand>.

#include <foundation/permissions/Permission.h>

namespace {
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};
struct StatefulBrand {
    int count = 0;
};
}  // namespace

int main() {
    return static_cast<int>(sizeof(::foundation::permissions::SharedPermission<Region, StatefulBrand>));
}
