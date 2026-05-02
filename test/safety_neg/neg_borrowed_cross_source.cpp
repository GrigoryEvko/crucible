// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #2 of 2 for safety::Borrowed (#1080).
//
// Premise: Borrowed<T, OwnerA> = Borrowed<T, OwnerB> (cross-source
// assignment) MUST be a compile error.  The Source phantom parameter
// is the load-bearing safety property — it tells the type system
// "this borrowed span comes from OwnerA, not OwnerB", and the
// compiler refuses any silent confusion between the two.
//
// Production scenario this prevents (per WRAP-Expr-3 + WRAP-SchemaTab):
//
//   Borrowed<const char, ExprPool> name_from_pool = ...;
//   Borrowed<const char, SchemaTable> name_from_schema = name_from_pool;  // BUG
//
// Today, both fields would be raw `const char*` and the assignment
// silently compiles.  After Borrowed wrapping, the assignment fails
// at the type level — the Source phantom changed from ExprPool to
// SchemaTable, and Borrowed<T, ExprPool> is a different
// instantiation than Borrowed<T, SchemaTable> (no implicit
// conversion between distinct template instantiations of the same
// class template).
//
// Expected diagnostic: "no match for 'operator='" / "no known
// conversion" pointing at the assignment site.  Borrowed<T, S>'s
// defaulted assignment operators only accept the same Borrowed<T, S>
// type — distinct Source parameters yield distinct types and the
// overload set is empty for the cross-source pair.

#include <crucible/safety/Borrowed.h>

namespace saf = crucible::safety;

struct ExprPool   { int dummy = 0; };
struct SchemaTable { int dummy = 0; };

int main() {
    char buf[8] = "hello";

    saf::Borrowed<const char, ExprPool>    name_from_pool{buf, 5};
    saf::Borrowed<const char, SchemaTable> name_from_schema{};

    // Bridge fires: cross-source assignment has no overload that
    // accepts the right-hand-side type.  The Source phantom
    // distinguishes the two instantiations, and there is no
    // implicit conversion from Borrowed<T, ExprPool> to
    // Borrowed<T, SchemaTable> — exactly what the wrapper exists
    // to prevent.
    name_from_schema = name_from_pool;
    (void)name_from_schema;
    return 0;
}
