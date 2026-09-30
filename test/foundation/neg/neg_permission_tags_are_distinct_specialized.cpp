// A split and a permission set ask tags_are_distinct whether no tag names
// one region twice.  This file tries to call every list distinct: it
// writes an explicit specialization of tags_are_distinct.
// tags_are_distinct is a function at namespace scope that is not a
// template, so no specialization matches it.

#include <foundation/permissions/Permission.h>

#include <initializer_list>
#include <meta>

template <>
consteval bool foundation::permissions::tags_are_distinct(std::initializer_list<std::meta::info>) {
    return true;
}

int main() { return 0; }
