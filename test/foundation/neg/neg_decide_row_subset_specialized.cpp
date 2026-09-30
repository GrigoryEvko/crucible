// The stage and pipeline gates ask decide::row_subset whether the rows of
// a payload fit in the row of a context.  This file tries to fit every
// payload: it writes an explicit specialization of row_subset.  row_subset
// is a function at namespace scope that is not a template, so no
// specialization matches it.

#include <foundation/contracts/Decide.h>

#include <meta>

template <>
consteval bool foundation::decide::row_subset(std::meta::info, std::meta::info) {
    return true;
}

int main() { return 0; }
