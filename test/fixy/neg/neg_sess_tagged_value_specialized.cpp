// The payload order asks tagged_value for the type that a Tagged drops to.
// This file tries to drop each Tagged to int: it writes an explicit
// specialization of tagged_value.  tagged_value is a function at
// namespace scope that is not a template, so no specialization matches
// it.

#include <fixy/session/Subtype.h>

#include <meta>

template <>
consteval std::meta::info fixy::session::detail::payload_order::tagged_value(std::meta::info) {
    return ^^int;
}

int main() { return 0; }
