// The row of a permission tag is the unique target of its edge, read by
// unique_target.  This file tries to give every tag the empty row: it
// writes an explicit specialization of unique_target.  unique_target is a
// function at namespace scope that is not a template, so no
// specialization matches it.

#include <foundation/diag/FailClosed.h>

#include <meta>

template <>
consteval std::meta::info foundation::fail_closed::unique_target(std::meta::info, std::meta::info) {
    return ^^void;
}

int main() { return 0; }
