// A split into many children and a combine of many children ask
// ctx_admits_each_tag_of whether the context admits the row of each
// child.  This file tries to admit every child: it writes an explicit
// specialization of ctx_admits_each_tag_of.  ctx_admits_each_tag_of is a
// function at namespace scope that is not a template, so no
// specialization matches it.

#include <foundation/permissions/Permission.h>

#include <meta>

template <>
consteval bool foundation::permissions::detail::ctx_admits_each_tag_of(std::meta::info, std::meta::info) {
    return true;
}

int main() { return 0; }
