// The fork mints ask each_body_takes_its_child whether each body takes the
// view of its own child.  This file tries to start any body: it writes an
// explicit specialization of each_body_takes_its_child.
// each_body_takes_its_child is a function at namespace scope that is not a
// template, so no specialization matches it.

#include <foundation/permissions/PermissionFork.h>

#include <meta>

template <>
consteval bool foundation::permissions::detail::each_body_takes_its_child(std::meta::info, std::meta::info,
                                                                          std::meta::info, std::meta::info) {
    return true;
}

int main() { return 0; }
