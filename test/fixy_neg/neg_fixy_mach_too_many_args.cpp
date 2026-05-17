// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-MINT-Mach fixture #2 (HS14 second-witness): mint_machine via
// fixy:: alias rejects when args... has the wrong ARITY for State's
// constructor — a distinct rejection class from the wrong-type fixture
// (neg_fixy_mach_construct_unbuildable).
//
// Violation: `mint_machine<State>(args...)` has
// `requires std::is_constructible_v<State, Args...>`.  Supplying 2
// args to a State that has only single-arg + default constructors
// fails the requires-clause for the (int, int) overload.  Routing
// through `fixy::mach::mint_machine` must reject identically.
//
// Expected diagnostic: "associated constraints are not satisfied" /
// "is_constructible" — the requires-clause names the constructibility
// trait.

#include <crucible/fixy/Mach.h>

namespace fmach = crucible::fixy::mach;

struct StateSingle {
    int x;
    constexpr StateSingle() noexcept             : x{0} {}
    constexpr StateSingle(int v) noexcept        : x{v} {}
    // NO two-arg constructor.
};

int main() {
    // Two int args → StateSingle: arity mismatch.
    [[maybe_unused]] auto bad = fmach::mint_machine<StateSingle>(1, 2);
    return 0;
}
