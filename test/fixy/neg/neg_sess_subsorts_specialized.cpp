// The payload order gives the answer for each pair through subsorts, and
// each gate of a payload reads that answer.  This file tries to make each
// payload a subsort of each other payload: it writes an explicit
// specialization of subsorts.  subsorts is a function at namespace scope
// that is not a template, so no specialization matches it.

#include <fixy/session/Refinement.h>

#include <meta>

template <>
consteval bool fixy::session::transition::subsorts(std::meta::info, std::meta::info, std::meta::info) {
    return true;
}

int main() { return 0; }
