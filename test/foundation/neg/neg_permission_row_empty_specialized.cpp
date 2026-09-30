// The mints without a context ask permission_row_empty whether a tag has
// the empty row.  This file tries to make each row read as empty: it
// writes an explicit specialization of permission_row_empty.
// permission_row_empty is a function at namespace scope that is not a
// template, so no specialization matches it.

#include <foundation/permissions/Permission.h>

#include <meta>

template <>
consteval bool foundation::permissions::permission_row_empty(std::meta::info) {
    return true;
}

int main() { return 0; }
