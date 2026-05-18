// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A3-015 (#1622): pins the `noexcept(T{})` operator-form gate that
// Capabilities.h uses to detect a future throwing-NSDMI regression in a
// cap::* atom.
//
// The production pin (`static_assert(noexcept(cap::Alloc{}))` etc. in
// detail::capabilities_self_test) rests on the C++ language guarantee
// that a default constructor declared noexcept(false) makes the
// `noexcept(T{})` operator return false.  This fixture witnesses the
// REJECTION side of that gate: it defines a structurally-identical
// `ThrowingCap` whose default ctor is `noexcept(false)`, and asserts
// `noexcept(ThrowingCap{})` — which MUST be false, so the static_assert
// MUST fail to compile.
//
// If this fixture ever silently compiles, then the language-level
// guarantee has shifted (or a future compiler quirk has been
// introduced) AND the production cap::* pins above would no longer be
// load-bearing — both go red together.  Defense in depth.
//
// Companion fixture: neg_capability_throwing_nsdmi_trait_pin.cpp pins
// the trait-form (std::is_nothrow_default_constructible_v<T>) of the
// same property.  The two fixtures together pin BOTH paths the
// production self-tests use to detect throwing-NSDMI regressions.
//
// Expected diagnostic: "static assertion failed" / "static_assert".

#include <crucible/effects/Capabilities.h>

struct ThrowingCap {
    ThrowingCap() noexcept(false) {}  // structurally cap::*-shaped, but
                                      // explicitly throwing
};

int main() {
    // Asserts a throwing default ctor is noexcept — it cannot be, so
    // the static_assert MUST fail to compile.  If it silently
    // succeeds, the underlying `noexcept(T{})` operator no longer
    // reflects T's noexcept-spec — load-bearing language guarantee
    // broken, production cap::* pins lose their meaning.
    static_assert(noexcept(ThrowingCap{}),
        "fixy-A3-015: noexcept(T{}) MUST be false when T's default "
        "ctor is noexcept(false); if this admits, the production "
        "cap::* noexcept pins are no longer load-bearing.");
    return 0;
}
