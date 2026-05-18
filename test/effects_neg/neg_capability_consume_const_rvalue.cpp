// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A3-012 (#1619): Capability::consume() is non-const rvalue-
// ref-qualified.  A const-rvalue Capability cannot bind to the
// non-const `&&` overload — silently adding a `const &&` overload
// later would defeat the linearity grep-discipline, so the type
// system must reject the const-rvalue call now.
//
// Violation: calling consume() on a const-rvalue.
//
// Expected diagnostic: "passing 'const Capability<...>' as 'this'
// argument discards qualifiers" / "no matching function for call to
// '...consume() const &&'" / "binding reference of type
// 'Capability<...>&&' to const expression" / similar.

#include <crucible/effects/Capability.h>

#include <utility>

namespace eff = crucible::effects;

int main() {
    auto bg = eff::testing::bg();
    eff::Capability<eff::Effect::Alloc, eff::Bg> const cap =
        eff::mint_cap<eff::Effect::Alloc>(bg);
    std::move(cap).consume();  // const-rvalue: non-const `&&` rejects
    return 0;
}
