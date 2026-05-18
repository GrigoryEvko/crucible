// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A3-015 (#1622): pins the trait-form (std::is_nothrow_default_-
// constructible_v<T>) gate that Capabilities.h uses to detect a future
// throwing-NSDMI regression in a cap::* atom.
//
// Companion to neg_capability_throwing_nsdmi_noexcept_pin.cpp: that
// fixture pins the `noexcept(T{})` operator path; this one pins the
// `<type_traits>` predicate path.  The two paths are nominally
// equivalent in standard C++, but defense-in-depth: if a future
// compiler / stdlib change ever introduces a discrepancy (e.g., a
// bug where one path admits a noexcept(false) default ctor while the
// other rejects), only ONE fixture fires — and the broken path is
// pinpointed.
//
// Without this fixture: a stdlib bug in `is_nothrow_default_-
// constructible_v` silently weakens the production trait pin without
// any visible warning.  WITH this fixture: the bug becomes a build-
// break, surfacing immediately.
//
// Expected diagnostic: "static assertion failed" / "static_assert".

#include <crucible/effects/Capabilities.h>

#include <type_traits>

struct ThrowingCap {
    ThrowingCap() noexcept(false) {}  // structurally cap::*-shaped, but
                                      // explicitly throwing
};

int main() {
    // Asserts a throwing default ctor is nothrow-default-constructible
    // per the type-trait — it cannot be, so the static_assert MUST
    // fail to compile.  If it silently succeeds, the trait no longer
    // reflects T's noexcept-spec — production cap::* trait pins lose
    // their meaning.
    static_assert(std::is_nothrow_default_constructible_v<ThrowingCap>,
        "fixy-A3-015: is_nothrow_default_constructible_v<T> MUST be "
        "false when T's default ctor is noexcept(false); if this "
        "admits, the production cap::* trait pins are no longer "
        "load-bearing.");
    return 0;
}
