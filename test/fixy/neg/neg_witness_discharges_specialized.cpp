// Discharges asks witness_discharges whether a presented value discharges
// a witness.  This file tries to discharge every witness with every value:
// it writes an explicit specialization of witness_discharges.
// witness_discharges is a function at namespace scope that is not a
// template, so no specialization matches it.

#include <fixy/Witnessed.h>

#include <meta>

template <>
consteval bool fixy::witness_discharges(std::meta::info, std::meta::info) {
    return true;
}

int main() { return 0; }
