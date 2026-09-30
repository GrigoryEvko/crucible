// A session mint asks permission_rows_of_set for the union of the rows of
// the regions that a protocol delivers.  This file tries to give every set
// the empty row: it writes an explicit specialization of
// permission_rows_of_set.  permission_rows_of_set is a function at
// namespace scope that is not a template, so no specialization matches it.

#include <fixy/session/Handle.h>

#include <meta>

template <>
consteval std::meta::info fixy::session::detail::permission_rows_of_set(std::meta::info) {
    return ^^foundation::effects::Row<>;
}

int main() { return 0; }
