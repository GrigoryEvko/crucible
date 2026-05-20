// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #2 of 2 for safety::WeakRef<T> (#1084 - WeakRef piece).
//
// Premise: WeakRef<T> is a strong newtype (TypeSafe axiom) — it must NOT
// implicitly decay to a raw `T*`.  The only ways to reach the underlying
// pointer are the NAMED, explicit escapes `try_get()` and `from_raw()`.
// An implicit `int* p = wr;` MUST fail to compile.
//
// Why it must reject: if WeakRef silently converted to a raw pointer, the
// whole null-discipline (forced check before deref, no confusion with an
// owning pointer) would leak away at the first assignment.  This is the
// CONVERSION rejection — distinct from fixture #1's CLASS-TEMPLATE
// constraint rejection: the class instantiates fine, but the implicit
// unwrap has no viable conversion (WeakRef ships no `operator T*`).
//
// Expected diagnostic: "cannot convert" / "no viable conversion" /
// "invalid conversion" pointing at the `int* leaked = wr` initialization.

#include <crucible/safety/WeakRef.h>

namespace saf = crucible::safety;

int main() {
    int x = 42;
    saf::WeakRef<int> wr{x};

    // Bridge fires: WeakRef<int> has no implicit operator int* — the
    // explicit escapes are wr.try_get() / wr.from_raw(...).  An implicit
    // unwrap to a raw pointer is a compile error.
    int* leaked = wr;
    (void)leaked;
    return 0;
}
