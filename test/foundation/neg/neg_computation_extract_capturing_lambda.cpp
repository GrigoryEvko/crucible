// Negative-compile test. This file must fail to compile.
//
// extract must not hand out a lambda that captures a capability.  The
// captures of a lambda are its members, so the walk over components finds
// the capability in the lambda, and the gate refuses it.  The lambda would
// otherwise leave a pure carrier as a plain value.

#include <foundation/effects/Capability.h>
#include <foundation/effects/Computation.h>
#include <foundation/effects/Effect.h>

#include <utility>

namespace fe = ::foundation::effects;

int main() {
    auto bg = fe::testing::bg();
    auto closure = [cap = fe::mint_cap<fe::Effect::IO>(bg)] { return sizeof(cap); };
    using Closure = decltype(closure);
    auto pure = fe::Computation<fe::Row<>, Closure>::mint_computation(std::move(closure));

    // The load-bearing line: must fail to compile.
    auto escaped = std::move(pure).extract();
    (void)escaped;
    return 0;
}
