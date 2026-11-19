// A gate that compares masks reads row_descriptor for the mask of a row.
// This file tries to give each row the empty mask: it writes an explicit
// specialization of row_descriptor.  row_descriptor is a function at
// namespace scope that is not a template, so no specialization matches
// it.

#include <foundation/effects/Row.h>

#include <meta>

template <>
consteval foundation::effects::EffectRowLattice::element_type foundation::effects::row_descriptor(std::meta::info) {
    return 0;
}

int main() { return 0; }
