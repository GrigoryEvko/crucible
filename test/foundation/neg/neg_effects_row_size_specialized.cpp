// The pure lift of Computation asks row_size whether a row is empty.  This
// file tries to make every row read as empty: it writes an explicit
// specialization of row_size.  row_size is a function at namespace scope
// that is not a template, so no specialization matches it.

#include <foundation/effects/Row.h>

#include <cstddef>
#include <meta>

template <>
consteval std::size_t foundation::effects::row_size(std::meta::info) {
    return 0;
}

int main() { return 0; }
