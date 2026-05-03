// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Phase 0 P0-1 fixture #2 of 2 for safety::fn::Fn (#1095) —
// proves that the Fn<Type, ...> aggregator rejects reference
// types in the Type slot.
//
// Why this matters: Fn::value_ is a value-type member.  A
// reference member (e.g., `int& value_`) MUST be initialized
// at construction and CANNOT be re-bound; the wrapper's move
// semantics, default constructor, and EBO discipline all
// silently break.  A reviewer who reaches for `Fn<int&>`
// because they want to "wrap a reference with grades" should
// instead use `Fn<NonNull<T>, ...>` (a refined non-null
// pointer), `Fn<Borrowed<T, Source>, ...>` (a lifetime-tagged
// borrow), or a Tagged-pointer wrapper — all of which are
// proper object types.
//
// Without the static_assert gate, instantiating Fn<int&> would
// fail downstream at member-init / move-construction sites
// with diagnostics like "cannot bind non-const lvalue
// reference to an rvalue", obscuring the root cause.  The
// gate catches it at the wrapper's instantiation site with a
// clear "requires Type to be a complete object type" message.
//
// Expected diagnostic: "requires Type to be a complete object
// type" / "Reject: void, reference types" pointing at the
// static_assert in safety/Fn.h's class-template body.

#include <crucible/safety/Fn.h>

namespace neg = crucible::safety::fn;

// Bridge fires: `sizeof(neg::Fn<int&>)` forces complete-class
// instantiation, which triggers the static_assert in the
// class-template body — std::is_object_v<int&> is false.
// Compilation aborts.
//
// (Declaring a pointer or reference to Fn<int&> would not force
// instantiation; only operations that require the complete class
// — sizeof, member access, value declaration — do.  sizeof is the
// minimal trigger that doesn't require the type to be
// constructible.)
static_assert(sizeof(neg::Fn<int&>) > 0,
              "the_fixture forces instantiation");

int main() { return 0; }
