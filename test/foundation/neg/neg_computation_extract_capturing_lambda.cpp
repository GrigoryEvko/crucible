// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// extract must not hand out a lambda that captures state.  GCC 16
// reflects no capture, so the walk over components cannot say what the
// lambda holds, and the gate refuses it.  A lambda that captures a
// capability would otherwise leave a pure carrier as a plain value.

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

    // THE LOAD-BEARING LINE: must FAIL to compile.
    auto escaped = std::move(pure).extract();
    (void)escaped;
    return 0;
}
