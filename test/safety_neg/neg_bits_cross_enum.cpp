// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #2 of 2 for safety::Bits<EnumType> (#1079).
//
// Premise: Bits<E1> | Bits<E2> with two DIFFERENT scoped-enum types
// MUST be a compile error.  The wrapper's friend operators only
// match the SAME instantiation; mixing two unrelated flag enums
// (e.g., NodeFlags | RecipeFlags) silently OR'd together would
// defeat the wrapper's primary purpose: preventing flag-set
// confusion across unrelated enum families.
//
// Without this rejection, a refactor that mistakenly OR's a
// NodeFlag value into a TraceRing op_flags Bits<OpFlags> field
// would silently produce wrong-bit-set without any compile-time
// audit — exactly the bug class the wrapper exists to prevent.
//
// Expected diagnostic: "no match for 'operator|'" /
// "no known conversion" pointing at the call site.  The friend
// operator| is defined inside Bits<EnumType> as
//   friend constexpr Bits operator|(Bits a, Bits b)
// So a call `bits_a | bits_b` where decltype(bits_a) =
// Bits<NodeFlags> and decltype(bits_b) = Bits<RecipeFlags> finds
// no overload that accepts both types — they are different
// template instantiations and the friend operators only see
// same-instantiation pairs (no implicit conversion exists between
// distinct Bits<E> instantiations).

#include <crucible/safety/Bits.h>

namespace saf = crucible::safety;

enum class NodeFlags : unsigned char {
    Constant = 0x01,
    Symbolic = 0x02,
};

enum class RecipeFlags : unsigned char {
    BitExact     = 0x01,
    Reproducible = 0x02,
};

int main() {
    saf::Bits<NodeFlags>   nf{NodeFlags::Constant};
    saf::Bits<RecipeFlags> rf{RecipeFlags::BitExact};

    // Bridge fires: operator| on two different Bits<E> instantiations
    // has no matching overload.  Even though both wrap unsigned char,
    // they are distinct types and the wrapper deliberately refuses
    // cross-enum mixing.
    auto bad = nf | rf;
    (void)bad;
    return 0;
}
