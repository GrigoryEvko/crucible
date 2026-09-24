// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A ReadLoan is one read right over a region, so its tag must be a
// Permission tag: an empty class that is not a union.  A loan over int
// names no region.
//
// Expected diagnostic: the static_assert in ReadLoan that asks for
// PermissionTag<Tag>.

#include <foundation/permissions/ReadView.h>

int main() {
    return static_cast<int>(sizeof(::foundation::permissions::ReadLoan<int>));
}
