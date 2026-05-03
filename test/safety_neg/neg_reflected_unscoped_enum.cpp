// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #1 of 2 for safety::reflected::bits_to_string (#1089).
//
// Premise: bits_to_string<UnscopedEnum>(...) MUST be a compile error.
// The ScopedEnum concept (Bits.h:161) rejects unscoped enums because
// they implicitly convert to/from integers and thus defeat the typed-
// bit-field discipline that Bits<E> exists to enforce.  An unscoped
// enum like
//
//   enum FlagsLegacy : unsigned char { LEGACY_A = 0x01, LEGACY_B = 0x02 };
//
// would silently allow `FlagsLegacy::LEGACY_A | 0x05` and equivalent
// integer-OR confusion — exactly the bug class WRAP-Foundation-Bits
// (#1079) ships the concept gate to prevent.  Reflected.h consumes
// that same concept on every public function; this fixture proves
// the gate fires at the bits_to_string entry point too.
//
// Without this rejection, a refactor that drops a legacy unscoped
// enum into a Bits<E> field and calls bits_to_string on it would
// (a) successfully instantiate Bits<UnscopedEnum> with no compile-
// time guard against integer mixing, then (b) successfully iterate
// reflection over the unscoped enum without surfacing the typing
// problem to anyone.  The concept gate cuts off both at the door.
//
// Expected diagnostic: "constraints not satisfied" /
// "ScopedEnum<...> evaluated to false" / "no matching function" at
// either the Bits<UnscopedEnum>{} ctor or the bits_to_string<...>
// instantiation, depending on which the compiler finishes diagnosing
// first.

#include <crucible/safety/Reflected.h>

#include <cstdio>

namespace ref = crucible::safety::reflected;

// Unscoped enum — NOT std::is_scoped_enum_v, so ScopedEnum rejects.
enum FlagsLegacy : unsigned char {
    LEGACY_A = 0x01,
    LEGACY_B = 0x02,
};

int main() {
    // Bridge fires: Bits<FlagsLegacy> is itself ill-formed because
    // ScopedEnum<FlagsLegacy> is false.  bits_to_string<FlagsLegacy>
    // therefore never gets a well-formed first parameter, and its
    // own template parameter constraint also fails.
    char buf[64] = {};
    auto n = ref::bits_to_string<FlagsLegacy>(
        crucible::safety::Bits<FlagsLegacy>{}, buf, sizeof(buf));
    std::printf("%zu\n", n);
    return 0;
}
