// The delegation mint asks is_delegatable_handle whether a handle stands
// outside every Loop with no brand.  This file tries to delegate any
// handle: it writes an explicit specialization of is_delegatable_handle.
// is_delegatable_handle is a function at namespace scope that is not a
// template, so no specialization matches it.

#include <fixy/session/Delegate.h>

#include <meta>

template <>
consteval bool fixy::session::detail::is_delegatable_handle(std::meta::info) {
    return true;
}

int main() { return 0; }
