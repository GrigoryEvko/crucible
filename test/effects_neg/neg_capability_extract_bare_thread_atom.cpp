// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// P1-AUDIT-4 (#855): extract_bare<>() is only defined for the
// value-token effects (Alloc / IO / Block).  Thread-effect atoms
// (Bg / Init / Test) have no value-level cap::* token and the
// extract_bare overloads do NOT match them — overload resolution
// fails.
//
// Violation: extract_bare(Capability<Effect::Bg, Bg>&&) — Effect::Bg
// is the thread-effect atom, no extract_bare overload accepts it.
//
// Expected diagnostic: "no matching function for call to" or
// "constraints not satisfied" — overload resolution exhausts the
// three Alloc/IO/Block overloads and finds none matching.

#include <crucible/effects/Capability.h>

namespace eff = crucible::effects;

int main() {
    eff::Bg bg;
    auto bg_cap = eff::mint_cap<eff::Effect::Bg>(bg);  // thread-effect cap
    auto bare   = eff::extract_bare(std::move(bg_cap));  // no matching overload
    (void)bare;
    return 0;
}
