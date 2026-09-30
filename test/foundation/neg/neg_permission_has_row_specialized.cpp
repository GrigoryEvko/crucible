// CtxAdmitsPermission asks has_permission_row whether a tag declares a
// row.  This file tries to give each tag a row: it writes an explicit
// specialization of has_permission_row.  has_permission_row is a function
// at namespace scope that is not a template, so no specialization matches
// it.

#include <foundation/permissions/Permission.h>

#include <meta>

template <>
consteval bool foundation::permissions::has_permission_row(std::meta::info) {
    return true;
}

int main() { return 0; }
