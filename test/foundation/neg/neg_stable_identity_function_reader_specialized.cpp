// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The identity walk reads the function that a pointer argument names, and
// it refuses a function with internal linkage.  A unit that could
// specialize the reader would put a function with external linkage in
// front of the walk, and a type that holds a pointer to an internal
// function would get an id.  The walk reads the function through an alias
// template, which has no specialization, so the specialization does not
// compile.

#include <foundation/reflect/Hash.h>

#include <meta>

namespace forged_reader {
static int hidden(int value) { return value; }
inline int shown(int value) { return value; }
}  // namespace forged_reader

template <>
inline constexpr std::meta::info foundation::reflect::detail::function_named_by_v<&forged_reader::hidden> =
    ^^forged_reader::shown;

int main() { return forged_reader::hidden(0); }
