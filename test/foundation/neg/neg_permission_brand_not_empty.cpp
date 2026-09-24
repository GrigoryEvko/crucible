// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A brand names one region, and it carries no state.  A brand with a
// data member would give a token bytes that no mint wrote.  No mint can
// build such a token, but the type is still spellable, and the IsBrand
// check in Permission's class body is what refuses the spelling.
//
// Expected diagnostic: the static_assert in Permission that asks for
// IsBrand<Brand>.

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
    return static_cast<int>(sizeof(::foundation::permissions::Permission<Region, StatefulBrand>));
}
