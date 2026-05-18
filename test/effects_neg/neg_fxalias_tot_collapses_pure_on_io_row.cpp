// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A3-019 audit fixture (pair 2/2): pins the load-bearing
// structural distinctness of `Pure<T>` vs `Tot<E_os, T>` when the
// row argument is non-empty.  Per CLAUDE.md §XVI the aliases are:
//
//     Pure<T>     ≡ Progress<Terminating, DetSafe<Pure, Computation<Row<>,    T>>>
//     Tot<E, T>   ≡ Progress<Terminating, DetSafe<Pure, Computation<E,        T>>>
//
// When E = Row<Effect::IO>, the inner `Computation` carries a row
// with IO atoms; this is structurally distinct from the empty-row
// Pure stack.  The alias substitution MUST preserve the row
// discrimination so an IO-effecting value cannot be silently
// retyped as Pure at the call site.
//
// Without the discipline, a `Tot<Row<IO>, int>` value could flow
// through a `Pure<int>` sink — exactly the F* lattice violation
// the substitution principle (Pure ⊑ Tot ⊑ Div ⊑ ST ⊑ All) is
// designed to catch.  Pure is the BOTTOM of the lattice; admitting
// IO at the Pure site would collapse the chain.
//
// Expected diagnostic: "static assertion failed" / "static_assert"
// / "fixy-A3-019" / "Pure-vs-Tot row discrimination"

#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/effects/FxAliases.h>

#include <type_traits>

namespace eff = ::crucible::effects;

// fixy-A3-019: the two aliases differ only in the inner row (∅ vs
// {IO}).  Same Progress class, same DetSafe tier, same payload —
// but the Computation row discriminates them.  Asserting same_v
// here fires the negative compile.
static_assert(
    std::is_same_v<eff::Pure<int>,
                   eff::Tot<eff::Row<eff::Effect::IO>, int>>,
    "fixy-A3-019: value-carrying F* aliases must remain structurally "
    "distinct under Computation row discrimination — Pure<T> carries "
    "the empty row and MUST NOT silently collapse with Tot<Row<IO>, "
    "T>.  Pure is the BOTTOM of the F* lattice (Pure ⊑ Tot ⊑ Div ⊑ "
    "ST ⊑ All); admitting IO at the Pure site would collapse the "
    "substitution chain, breaking the row-effect guarantee the "
    "wrapper-nesting canonical order is designed to enforce.");

int main() { return 0; }
