// A local choice and a multiparty binding ask has_session_network whether
// a carrier states its network.  This file tries to make each carrier
// state one: it writes an explicit specialization of has_session_network.
// has_session_network is a function at namespace scope that is not a
// template, so no specialization matches it.

#include <fixy/session/NetworkModel.h>

#include <meta>

template <>
consteval bool fixy::session::has_session_network(std::meta::info) {
    return true;
}

int main() { return 0; }
