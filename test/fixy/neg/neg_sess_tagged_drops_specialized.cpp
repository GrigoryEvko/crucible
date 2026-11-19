// The payload order asks tagged_drops whether a Tagged drops to its value
// type.  This file tries to drop an External tag too: it writes an
// explicit specialization of tagged_drops.  tagged_drops is a function at
// namespace scope that is not a template, so no specialization matches
// it.

#include <fixy/session/Subtype.h>

#include <meta>

template <>
consteval bool fixy::session::detail::payload_order::tagged_drops(std::meta::info) {
    return true;
}

int main() { return 0; }
