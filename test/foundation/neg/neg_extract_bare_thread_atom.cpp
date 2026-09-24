// A thread atom has no value-level tag, so extract_bare is not in the
// overload set for it.  The rejection is substitution failure on the
// HasBareTag constraint, and not a hard error inside the body.

#include <foundation/effects/Capability.h>

#include <utility>

namespace fe = ::foundation::effects;

int main() {
    auto bg = fe::testing::bg();
    auto bg_cap = fe::mint_cap<fe::Effect::Bg>(bg);
    [[maybe_unused]] auto bare = fe::extract_bare(std::move(bg_cap));
    return 0;
}
