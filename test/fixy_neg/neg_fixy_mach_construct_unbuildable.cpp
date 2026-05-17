// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-MINT-Mach fixture #2: mint_machine via fixy:: alias rejects
// when State is not constructible from the supplied args.
//
// Violation: `mint_machine<State>(args...)` has
// `requires std::is_constructible_v<State, Args...>`.  Passing a
// const char* to a State requiring an int fails the requires-clause.
// Routing through `fixy::mach::mint_machine` must reject identically.
//
// Expected diagnostic: "associated constraints are not satisfied"
// pointing at is_constructible.

#include <crucible/fixy/Mach.h>

namespace fmach = crucible::fixy::mach;

struct StateInt {
    int x;
    constexpr StateInt(int v) noexcept : x{v} {}
};

int main() {
    // const char* → StateInt: not constructible.
    [[maybe_unused]] auto bad = fmach::mint_machine<StateInt>("nope");
    return 0;
}
