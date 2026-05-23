// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// WRAP-Reflect-4 #985, mismatch class #2 of 2:
// FUNCTION TYPES (NOT pointers) ARE EXCLUDED FROM THE ALLOW-LIST.
//
// `detail_reflect::IsReflectFieldSupported<T>` admits function
// POINTERS via std::is_pointer_v (a function pointer IS a
// pointer), but NOT bare function types — a function type cannot
// be stored as a struct member or passed by value, so reflection
// cannot meaningfully dispatch on it.  This distinction matters
// because if a future contributor extends the concept to include
// std::is_function_v, the hash_field / pack_field / print_field
// helpers would compile but produce nonsense results — the bit-
// cast / fmix64 path doesn't apply to function types.
//
// Distinct from the void fixture, which fails because void has
// no first-class representation; here both function pointers AND
// function types exist as first-class entities but only the
// pointer form is reflectable.
//
// Expected diagnostic: static assertion failed / WRAP-Reflect-4 /
// IsReflectFieldSupported.

#include <crucible/Reflect.h>

// Should FAIL at compile time: function types are excluded.
// (Function POINTERS would satisfy std::is_pointer_v and pass.)
using FuncType = int(int, double);
static_assert(::crucible::detail_reflect::IsReflectFieldSupported<FuncType>,
    "WRAP-Reflect-4 #985 fixture: this static_assert MUST fail "
    "at compile time because function types are excluded from "
    "the IsReflectFieldSupported allow-list (only function "
    "POINTERS are admitted via std::is_pointer_v).");

int main() { return 0; }
