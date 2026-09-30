// A combine asks perm_brands_agree whether its children come from one
// region.  This file tries to combine the children of two regions: it
// writes an explicit specialization of perm_brands_agree.
// perm_brands_agree is a function at namespace scope that is not a
// template, so no specialization matches it.

#include <foundation/permissions/Permission.h>

#include <initializer_list>
#include <meta>

template <>
consteval bool foundation::permissions::detail::perm_brands_agree(std::initializer_list<std::meta::info>) {
    return true;
}

int main() { return 0; }
