// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// IsReflectFieldSupported, mismatch class #2 of 2:
// FUNCTION TYPES ARE EXCLUDED FROM THE ALLOW-LIST.
//
// `detail_reflect::IsReflectFieldSupported<T>` does not admit a bare
// function type: a function type cannot be stored as a struct member
// or passed by value, so reflection cannot dispatch on it.  If a
// future contributor extends the concept to include
// std::is_function_v, the hash_field / pack_field helpers would
// compile but produce nonsense results, because the bit-cast /
// fmix64 path does not apply to function types.  The concept does
// not admit a function pointer either, because a pointer holds an
// address.
//
// Distinct from the void fixture, which fails because void has
// no first-class representation.
//
// Expected diagnostic: static assertion failed, required by the
// constraints of IsReflectFieldSupported.

#include <crucible/Reflect.h>

// Should FAIL at compile time: function types are excluded.
using FuncType = int(int, double);
static_assert(::crucible::detail_reflect::IsReflectFieldSupported<FuncType>,
              "This static_assert MUST fail "
              "at compile time because function types are excluded from "
              "the IsReflectFieldSupported allow-list.");

int main() { return 0; }
