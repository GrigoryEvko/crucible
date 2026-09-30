// A Continue asks perm_set_equal whether an iteration ends with the set of
// its entry.  This file tries to let an iteration keep a permission: it
// writes an explicit specialization of perm_set_equal.  perm_set_equal is
// a function at namespace scope that is not a template, so no
// specialization matches it.

#include <foundation/permissions/PermSet.h>

#include <meta>

template <>
consteval bool foundation::permissions::perm_set_equal(std::meta::info, std::meta::info) {
    return true;
}

int main() { return 0; }
