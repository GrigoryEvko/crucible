// Every mint of a session asks well_formed_of whether a protocol is
// well-formed.  This file tries to make each protocol well-formed: it
// writes an explicit specialization of well_formed_of.  well_formed_of is
// a function at namespace scope that is not a template, so no
// specialization matches it.

#include <fixy/session/Protocol.h>

#include <meta>

template <>
consteval bool fixy::session::detail::well_formed_of(std::meta::info, std::meta::info) {
    return true;
}

int main() { return 0; }
