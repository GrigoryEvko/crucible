// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// WRAP-Reflect-4 #985, mismatch class #1 of 2:
// `void` IS NOT IN THE REFLECT FIELD-SUPPORTED ALLOW-LIST.
//
// `detail_reflect::IsReflectFieldSupported<T>` enumerates the
// allowable type categories (enum / integral / floating_point /
// pointer / array / class).  `void` falls into none of those, so
// the concept evaluates to false; the static_assert below
// SHOULD fail at compile time.  If it starts compiling, the
// concept silently widened to admit unsupported types — the
// allow-list's bounded universe assumption broke and the three
// field-helpers' static_assert(false) fall-throughs no longer
// have a meaningful concept name to reference.
//
// Distinct from the function-type fixture, which fails because
// FUNCTION types are excluded but function-POINTERS are admitted;
// here the failure is on `void` itself, which has no first-class
// representation.
//
// Expected diagnostic: static assertion failed / WRAP-Reflect-4 /
// IsReflectFieldSupported.

#include <crucible/Reflect.h>

// Should FAIL at compile time: `void` is not in the allow-list.
static_assert(::crucible::detail_reflect::IsReflectFieldSupported<void>,
    "WRAP-Reflect-4 #985 fixture: this static_assert MUST fail "
    "at compile time because `void` is excluded from the "
    "IsReflectFieldSupported allow-list.");

int main() { return 0; }
