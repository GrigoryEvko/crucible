// A permission tag has a row by edge when has_edge_from finds its edge.
// This file tries to give every tag an edge: it writes an explicit
// specialization of has_edge_from.  has_edge_from is a function at
// namespace scope that is not a template, so no specialization matches
// it.

#include <foundation/diag/FailClosed.h>

#include <meta>

template <>
consteval bool foundation::fail_closed::has_edge_from(std::meta::info, std::meta::info) {
    return true;
}

int main() { return 0; }
