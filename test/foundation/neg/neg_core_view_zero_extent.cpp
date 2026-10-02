// A View of a fixed extent always borrows its elements.  A fixed extent of
// zero borrows nothing and has no element to point at, so the extent gate
// refuses it.  An empty run is a View of the dynamic extent.

#include <foundation/core/Region.h>

int main() { return static_cast<int>(sizeof(::foundation::core::View<int, 0>)); }
