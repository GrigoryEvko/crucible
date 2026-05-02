// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #1 of 2 for safety::Bits<EnumType> (#1079).
//
// Premise: Bits<E>{42} (raw integer literal as ctor arg) MUST be a
// compile error.  The wrapper's job is to prevent raw integers from
// silently entering the typed bit-field surface — only EnumType
// values (via initializer_list) and the explicit
// `Bits<E>::from_raw(underlying)` factory may produce a Bits<E>.
//
// Without this rejection, a refactor that drops a stale `0x05`
// constant into a Bits<E> field would silently bypass enum-typed
// auditing — exactly the bug class the wrapper exists to prevent.
//
// Expected diagnostic: "no matching function" / "could not convert"
// / "no known conversion" pointing at the Bits<E>{int} construction
// site.  The two public ctors are
//   - Bits() noexcept (default)
//   - Bits(std::initializer_list<EnumType>) noexcept
// Neither accepts an int.  initializer_list<EnumType>{42} requires
// the brace-elision-of-int-into-EnumType conversion, which is
// rejected because scoped enum classes do NOT implicitly convert
// from integer literals.

#include <crucible/safety/Bits.h>

namespace saf = crucible::safety;

enum class TestFlags : unsigned char {
    A = 0x01,
    B = 0x02,
    C = 0x04,
};

int main() {
    // Bridge fires: int literal is not constructible into Bits<TestFlags>.
    // The initializer_list<TestFlags> ctor is the only public single-arg
    // ctor; brace-init from int is rejected because scoped enum class
    // refuses implicit-from-int conversion.
    saf::Bits<TestFlags> bad{42};
    (void)bad;
    return 0;
}
