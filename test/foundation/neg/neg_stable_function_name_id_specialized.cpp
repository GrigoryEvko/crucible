// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The content key of the computation cache folds the name id of its
// function.  A unit that could specialize the name id would give one
// function the key of another, and the cache would hand one function the
// result of the other.  The name id is a function that is not a template,
// so the specialization does not compile.

#include <foundation/reflect/Hash.h>

#include <cstdint>

namespace forged_function {
inline int first(int value) { return value; }
}  // namespace forged_function

template <>
inline constexpr std::uint64_t foundation::reflect::stable_function_name_id<&forged_function::first> = 1;

int main() { return forged_function::first(0); }
