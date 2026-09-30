// IsConcurrentRow asks is_concurrent_row whether a type is a concurrent
// row.  This file tries to make each type one: it writes an explicit
// specialization of is_concurrent_row.  is_concurrent_row is a function at
// namespace scope that is not a template, so no specialization matches it.

#include <foundation/effects/Concurrent.h>

#include <meta>

template <>
consteval bool foundation::effects::is_concurrent_row(std::meta::info) {
    return true;
}

int main() { return 0; }
