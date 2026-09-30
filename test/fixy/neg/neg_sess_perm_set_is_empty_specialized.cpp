// The delegation mint asks perm_set_is_empty whether a handle holds no
// permission.  This file tries to make each set read as empty: it writes
// an explicit specialization of perm_set_is_empty.  perm_set_is_empty is a
// function at namespace scope that is not a template, so no
// specialization matches it.

#include <fixy/session/Handle.h>

#include <meta>

template <>
consteval bool fixy::session::detail::perm_set_is_empty(std::meta::info) {
    return true;
}

int main() { return 0; }
