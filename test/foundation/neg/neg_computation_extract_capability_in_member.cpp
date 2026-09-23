// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// extract must not hand out a capability that a plain class holds in a
// member.  The class names no capability in a template argument, and the
// list of authority kinds does not name the class.  The walk over the
// components of the payload reads the member and finds the capability.
//
// Sibling of neg_computation_extract_capability_payload.cpp, where the
// payload is the capability itself.

#include <foundation/effects/Capability.h>
#include <foundation/effects/Computation.h>
#include <foundation/effects/Effect.h>

#include <utility>

namespace fe = ::foundation::effects;

namespace {

using IoCap = fe::Capability<fe::Effect::IO, fe::Bg>;

// A carrier that no list names.
struct HoldsCapability {
    int tag = 0;
    IoCap held;
};

using PureOverHolder = fe::Computation<fe::Row<>, HoldsCapability>;

}  // namespace

int main() {
    auto bg = fe::testing::bg();
    PureOverHolder pure = PureOverHolder::mint_computation(HoldsCapability{0, fe::mint_cap<fe::Effect::IO>(bg)});

    // THE LOAD-BEARING LINE: must FAIL to compile.
    auto escaped = std::move(pure).extract();
    (void)escaped;
    return 0;
}
