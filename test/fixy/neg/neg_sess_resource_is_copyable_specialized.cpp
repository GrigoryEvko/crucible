// Every mint of a session asks resource_is_copyable whether a Resource can
// be copied.  This file tries to make each Resource read as one that
// cannot be copied: it writes an explicit specialization of
// resource_is_copyable.  resource_is_copyable is a function at namespace
// scope that is not a template, so no specialization matches it.

#include <fixy/session/Handle.h>

#include <meta>

template <>
consteval bool fixy::session::resource_is_copyable(std::meta::info) {
    return false;
}

int main() { return 0; }
