// A send asks perm_set_subset whether the sender holds what the payload
// takes.  This file tries to let a sender send what it does not hold: it
// writes an explicit specialization of perm_set_subset.  perm_set_subset
// is a function at namespace scope that is not a template, so no
// specialization matches it.

#include <foundation/permissions/PermSet.h>

#include <meta>

template <>
consteval bool foundation::permissions::perm_set_subset(std::meta::info, std::meta::info) {
    return true;
}

int main() { return 0; }
