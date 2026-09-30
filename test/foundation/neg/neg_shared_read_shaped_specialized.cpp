// with_shared_read asks with_shared_read_shaped whether it lends from a
// pool with a body that takes a share.  This file tries to lend from any
// class: it writes an explicit specialization of with_shared_read_shaped.
// with_shared_read_shaped is a function at namespace scope that is not a
// template, so no specialization matches it.

#include <foundation/permissions/Permission.h>

#include <initializer_list>
#include <meta>

template <>
consteval bool foundation::permissions::detail::with_shared_read_shaped(std::initializer_list<std::meta::info>) {
    return true;
}

int main() { return 0; }
