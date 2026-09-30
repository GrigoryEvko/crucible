// A permission hold asks perm_set_contains whether its set names a tag
// before it gives the token out.  This file tries to make every set name
// every tag: it writes an explicit specialization of perm_set_contains.
// perm_set_contains is a function at namespace scope that is not a
// template, so no specialization matches it.

#include <foundation/permissions/PermSet.h>

#include <meta>

template <>
consteval bool foundation::permissions::perm_set_contains(std::meta::info, std::meta::info) {
    return true;
}

int main() { return 0; }
