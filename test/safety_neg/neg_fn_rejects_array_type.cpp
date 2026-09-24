// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Phase 0 P0-1 audit-round-2 fixture #5 of 5 for safety::fn::Fn
// (#1095) — proves that the Fn<Type, ...> aggregator rejects
// C array Type.
//
// Why this matters: `std::is_object_v<int[N]>` is TRUE — C arrays
// ARE object types — so the existing `is_object_v<Type>` gate
// admits `Fn<int[5]>`.  But the wrapper's value-constructor
// `Fn(Type v)` declares parameter `v` of type `int[5]`, which
// silently DECAYS to `int*` per [conv.array].  The constructor
// body then attempts `value_{std::move(v)}` — initializing an
// `int[5]` member from `int*&&`, which is ill-formed.
//
// The user's intent is one of two distinct things:
//
//   (a) Fixed-size value array → use Fn<std::array<T, N>, ...>
//       which is a proper class type with copy/move/value
//       semantics intact.
//
//   (b) Borrowed view of an external array → use
//       Fn<Borrowed<T, Source>, ...> with the lifetime tag
//       carrying the borrow's source.
//
// Without the static_assert gate, `Fn<int[5]>` would either
// fail noisily at the constructor body (with "cannot initialize
// int[5] from int*") or, worse, succeed at default construction
// (the `value_{}` value-initialization of an array IS valid)
// while the value-constructor remains effectively unusable.
// The fragmented behavior is exactly the footgun this gate
// catches at the wrapper instantiation site.
//
// Expected diagnostic: "Fn<T[N], ...> is malformed.  C arrays
// decay to pointers in function parameters".

#include <crucible/safety/Fn.h>

namespace neg = crucible::safety::fn;

// Bridge fires: instantiating Fn<int[5]> trips the
// !std::is_array_v<Type> static_assert in the class-template
// body.  Compilation aborts with the assertion message.
[[maybe_unused]] neg::Fn<int[5]> the_fixture{};

int main() { return 0; }
